"""Local-only checks for opt-in unverified ranking storage and UDP protocol."""
import importlib.util
from contextlib import closing
from pathlib import Path
import secrets
import sqlite3
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("rankings_test_subject",ROOT/"server/lobby/rankings.py")
rankings = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rankings)


class RankingServiceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="hotd2-rankings-")
        self.path = str(Path(self.temp.name)/"rankings.sqlite3")
        self.service = rankings.Rankings(self.path)
        self.addr = ("127.0.0.1",30001)
        self.build = 2026102001
        self.serial = 0

    def tearDown(self):
        self.service.close()
        self.temp.cleanup()

    def header(self,kind,version=1):
        self.serial += 1
        return b"H2RK"+bytes([version,kind])+struct.pack("<I",self.serial)

    def cookie(self,address=None,version=1):
        response = self.service.handle(self.header(1,version),address or self.addr)
        self.assertEqual(len(response),26)
        self.assertEqual(response[:6],b"H2RK"+bytes([version,101]))
        return response[10:26]

    def submit(self,table=0,score=100,rounds=0,difficulty=2,players=1,name=b"AMS",run=None,address=None,build=None,version=1):
        address = address or self.addr
        submit = rankings.SUBMIT_V2 if version==2 else rankings.SUBMIT
        self.assertLessEqual(len(name),12 if version==2 else 3,"test helper must not truncate the submitted name")
        body = submit.pack(build or self.build,table,name,score,rounds,difficulty,players,run or secrets.token_bytes(16))
        response = self.service.handle(self.header(3,version)+body+self.cookie(address,version),address)
        self.assertEqual(len(response),11)
        self.assertEqual(response[:6],b"H2RK"+bytes([version,103]))
        return response[-1]

    def query(self,table=0,build=None,version=1):
        response = self.service.handle(self.header(2,version)+struct.pack("<IB",build or self.build,table)+self.cookie(version=version),self.addr)
        self.assertLessEqual(len(response),217 if version==2 else 127)
        self.assertEqual(response[:6],b"H2RK"+bytes([version,102]))
        self.assertEqual(struct.unpack_from("<IB",response,10),(build or self.build,table))
        self.assertEqual(response[15],0)
        row = rankings.ROW_V2 if version==2 else rankings.ROW
        self.assertEqual(len(response),17+response[16]*row.size)
        result = [row.unpack_from(response,17+i*row.size) for i in range(response[16])]
        return [(name.rstrip(b"\0"),*values) for name,*values in result] if version==2 else result

    def refill(self):
        self.service.budgets.clear()

    def test_round_trip_and_persistence(self):
        self.assertEqual(self.submit(score=900),0)
        self.assertEqual(self.query()[0],(b"AMS",900,0,2,1))
        self.service.close()
        self.service = rankings.Rankings(self.path)
        self.assertEqual(self.query()[0][1],900)

    def test_online_names_and_legacy_queries(self):
        for name,score in ((b"AMS",100),(b"LONG_NAME-12",200),(b"Z",300),(b"A B._-09",400)):
            self.refill();self.assertEqual(self.submit(name=name,score=score,version=2),0)
        self.assertEqual([r[0] for r in self.query(version=2)],[b"A B._-09",b"Z",b"LONG_NAME-12",b"AMS"])
        self.assertEqual([r[0] for r in self.query()],[b"A B",b"Z  ",b"LON",b"AMS"])
        self.assertEqual(self.service.db.execute("SELECT name FROM rankings_v1 ORDER BY score DESC").fetchall(),
                         [(b"A B._-09",),(b"Z",),(b"LONG_NAME-12",),(b"AMS",)])
        self.service.close();self.service=rankings.Rankings(self.path)
        self.assertEqual(self.query(version=2)[2][0],b"LONG_NAME-12")
        # Leading spaces are legal; v1 projection must still be nonblank.
        self.refill();self.assertEqual(self.submit(name=b"   AMS_LONG",score=500,version=2),0)
        self.assertEqual(self.query()[0][0],b"AMS")
        self.assertEqual(self.query(version=2)[0][0],b"   AMS_LONG")

    def test_v2_length_characters_and_padding(self):
        for name in (b"",b" "*12,b"lower",b"A!B",b"A\xffB",b"A\0B",b"A\tB"):
            self.refill();self.assertEqual(self.submit(name=name,version=2),2)
        run=secrets.token_bytes(16)
        self.assertEqual(self.service._submit(self.build,0,b"ABCDEFGHIJKLM",100,0,2,1,run),2)
        # A 13-byte wire field is rejected as malformed instead of being truncated.
        cookie=self.cookie(version=2)
        body=struct.pack("<IB13sIHBB16s",self.build,0,b"ABCDEFGHIJKLM",100,0,2,1,run)
        self.assertIsNone(self.service.handle(self.header(3,2)+body+cookie,self.addr))
        self.assertEqual(self.query(version=2),[])
        self.refill();self.assertEqual(self.submit(name=b"ABCDEFGHIJKL",version=2),0)
        self.assertEqual(self.query(version=2)[0][0],b"ABCDEFGHIJKL")

    def test_v1_v2_retry_identity_and_response_capacity(self):
        run=secrets.token_bytes(16)
        self.assertEqual(self.submit(name=b"AMS",run=run),0)
        self.assertEqual(self.submit(name=b"AMS",run=run,version=2),1)
        self.refill();self.assertEqual(self.submit(name=b"AMS_LONG",run=run,version=2),2)
        for score in range(1,11):
            self.refill();self.assertEqual(self.submit(table=1,score=score,name=b"LONG_NAME-12",version=2),0)
        self.refill();self.assertEqual(len(self.query(table=1,version=2)),10)
        self.assertEqual(len(self.query(table=1)),10)

    def test_migrate_three_byte_database_preserves_existing_records(self):
        self.service.close()
        legacy_schema=rankings.SCHEMA.replace("length(name) BETWEEN 1 AND 12","length(name)=3")
        records=[(b"A"*16,self.build,0,b"OLD",900,0,2,1,12345),
                 (b"B"*16,self.build,2,b"Z  ",500,8,4,2,12346)]
        with closing(sqlite3.connect(self.path)) as db, db:
            db.execute("DROP TABLE rankings_v1")
            db.execute(legacy_schema)
            db.execute("CREATE INDEX rankings_board ON rankings_v1(build,board)")
            db.executemany("INSERT INTO rankings_v1 VALUES (?,?,?,?,?,?,?,?,?)",records)
        self.service=rankings.Rankings(self.path)
        self.assertEqual(self.service.db.execute("SELECT * FROM rankings_v1 ORDER BY run").fetchall(),records)
        self.assertEqual(self.query(version=2)[0][0],b"OLD")
        self.assertEqual(self.query(table=2)[0][0],b"Z  ")
        self.assertEqual(self.submit(name=b"OLD",score=900,run=b"A"*16,version=2),1)
        self.refill();self.assertEqual(self.submit(name=b"LONG_NAME-12",version=2),0)
        self.assertIsNotNone(self.service.db.execute("SELECT 1 FROM sqlite_master WHERE type='index' AND name='rankings_board'").fetchone())
        self.assertIsNone(self.service.db.execute("SELECT 1 FROM sqlite_master WHERE name='rankings_name3_legacy'").fetchone())
        self.service.close();self.service=rankings.Rankings(self.path)
        self.assertEqual(self.service.db.execute("SELECT COUNT(*) FROM rankings_v1").fetchone()[0],3)

    def test_migration_failure_rolls_back_original_table(self):
        self.service.close()
        legacy_schema=rankings.SCHEMA.replace("length(name) BETWEEN 1 AND 12","length(name)=3")
        with closing(sqlite3.connect(self.path)) as db, db:
            db.execute("DROP TABLE rankings_v1")
            db.execute(legacy_schema)
            db.execute("PRAGMA ignore_check_constraints=ON")
            db.execute("INSERT INTO rankings_v1 VALUES (?,?,?,?,?,?,?,?,?)",(b"X"*16,self.build,0,b"OLD",100,0,2,3,12345))
        with self.assertRaises(sqlite3.IntegrityError):
            rankings.Rankings(self.path)
        with closing(sqlite3.connect(self.path)) as db, db:
            self.assertIn("length(name)=3",db.execute("SELECT sql FROM sqlite_master WHERE name='rankings_v1'").fetchone()[0])
            self.assertEqual(db.execute("SELECT name,players FROM rankings_v1").fetchall(),[(b"OLD",3)])
            self.assertIsNone(db.execute("SELECT 1 FROM sqlite_master WHERE name='rankings_name3_legacy'").fetchone())

    def test_board_build_isolation_and_sort_direction(self):
        for table,score,rounds in ((0,100,0),(0,200,0),(3,800,0),(3,400,0),(2,1000,2),(2,10,3)):
            self.refill();self.assertEqual(self.submit(table=table,score=score,rounds=rounds),0)
        self.refill();self.assertEqual([r[1] for r in self.query(0)],[200,100])
        self.refill();self.assertEqual([r[1] for r in self.query(3)],[400,800])
        self.refill();self.assertEqual([r[2] for r in self.query(2)],[3,2])
        self.refill();self.assertEqual(self.query(0,self.build+1),[])

    def test_retry_dedup_and_conflicting_run_id(self):
        run = secrets.token_bytes(16)
        self.assertEqual(self.submit(run=run),0)
        self.assertEqual(self.submit(run=run),1)
        self.refill();self.assertEqual(self.submit(run=run,score=200),2)
        self.refill();self.assertEqual(len(self.query()),1)

    def test_compatible_releases_share_existing_records_without_rewriting(self):
        # Model a database populated before the fix, including both architectures
        # and separate run IDs from the same display name. Opening/querying must
        # neither migrate nor deduplicate those independently completed runs.
        builds = (2026102200,2026102201,2026102300,2026102301)
        records = [(bytes([index+1])*16,build,0,b"GITFIST",100+index,0,2,1,12345+index)
                   for index,build in enumerate(builds)]
        with self.service.db:
            self.service.db.executemany("INSERT INTO rankings_v1 VALUES (?,?,?,?,?,?,?,?,?)",records)
        self.service.close();self.service=rankings.Rankings(self.path)
        for build in (*builds,rankings.LEADERBOARD_COMPATIBILITY_ID):
            self.refill()
            self.assertEqual([row[1] for row in self.query(build=build,version=2)],[103,102,101,100])
        self.assertEqual(self.service.db.execute("SELECT * FROM rankings_v1 ORDER BY run").fetchall(),records)
        # Legacy retries still match their original build and consume no write
        # token. Reusing their identity for a different build remains a conflict.
        self.refill()
        self.assertEqual(self.submit(build=builds[0],name=b"GITFIST",score=100,run=records[0][0],version=2),1)
        self.assertEqual(self.submit(build=rankings.LEADERBOARD_COMPATIBILITY_ID,name=b"GITFIST",score=100,run=records[0][0],version=2),2)
        self.assertEqual(self.submit(build=rankings.LEADERBOARD_COMPATIBILITY_ID,name=b"NEW",score=200,version=2),0)
        self.refill()
        self.assertEqual([row[1] for row in self.query(build=builds[0])],[200,103,102,101,100])
        # Unknown/explicitly incompatible namespaces and different game modes
        # must not accidentally join the current board.
        self.assertEqual(self.query(build=2,version=2),[])
        self.refill();self.assertEqual(self.query(build=2026102400,version=2),[])
        self.assertEqual(self.query(table=1,build=rankings.LEADERBOARD_COMPATIBILITY_ID,version=2),[])

    def test_compatible_boards_merge_before_sorting_and_limiting(self):
        builds = (2026102200,2026102301,rankings.LEADERBOARD_COMPATIBILITY_ID)
        for index in range(12):
            build=builds[index%len(builds)]
            for board,score,rounds in ((0,100+index,0),(2,1000-index,index+1),(3,100+index,0)):
                self.assertEqual(self.service._submit(build,board,b"RUN",score,rounds,2,1,secrets.token_bytes(16)),0)
        self.assertEqual([row[1] for row in self.query(build=builds[0])],list(range(111,101,-1)))
        self.assertEqual([row[2] for row in self.query(table=2,build=builds[1])],list(range(12,2,-1)))
        self.refill()
        self.assertEqual([row[1] for row in self.query(table=3,build=builds[2])],list(range(100,110)))
        # Per-source retention is unchanged: an update does not prune away
        # older releases' records, even as new scores fill the stable board.
        with patch.object(rankings,"MAX_ROWS_PER_BOARD",1):
            self.assertEqual(self.service._submit(builds[2],0,b"TOP",999,0,2,1,secrets.token_bytes(16)),0)
        self.assertEqual(self.service.db.execute("SELECT COUNT(*) FROM rankings_v1 WHERE build=? AND board=0",(builds[0],)).fetchone()[0],4)

    def test_eligibility_and_fixed_bounds(self):
        cases = [dict(score=0),dict(table=13),dict(name=b"a!?"),dict(name=b"   "),dict(table=2),
                 dict(table=0,rounds=1),dict(table=3,players=2),dict(table=3,score=1296001),dict(difficulty=7)]
        for case in cases:
            self.refill();self.assertEqual(self.submit(**case),2)
        self.assertEqual(self.query(),[])

    def test_cookies_bind_endpoint_and_expire(self):
        cookie = self.cookie()
        request = self.header(2)+struct.pack("<IB",self.build,0)+cookie
        self.assertIsNone(self.service.handle(request,(self.addr[0],self.addr[1]+1)))
        with patch.object(rankings.time,"time",return_value=rankings.time.time()+2*rankings.COOKIE_SECONDS+1):
            self.assertIsNone(self.service.handle(request,self.addr))
        self.assertIsNone(self.service.handle(request[:-1],self.addr))
        self.assertIsNone(self.service.handle(request+b"x",self.addr))

    def test_top_rows_and_table_capacity(self):
        with patch.object(rankings,"MAX_ROWS_PER_BOARD",3):
            for score in (100,400,300,200):
                self.refill();self.assertEqual(self.submit(score=score),0)
            self.refill();self.assertEqual([r[1] for r in self.query()],[400,300,200])
            self.refill();self.assertEqual(self.submit(score=10),4)
        for score in range(11):
            self.refill();self.submit(table=1,score=score+1)
        self.refill();self.assertEqual(len(self.query(1)),10)

    def test_opt_in_and_rate_caps(self):
        disabled = rankings.Rankings()
        cookie = disabled.handle(self.header(1),self.addr)[10:]
        result = disabled.handle(self.header(2)+struct.pack("<IB",self.build,0)+cookie,self.addr)
        self.assertEqual((result[15],result[16]),(3,0))
        self.assertEqual(self.submit(),0)
        self.assertEqual(self.submit(),0)
        self.assertEqual(self.submit(),5)
        with patch.object(rankings,"MAX_TRACKED_IPS",1):
            self.assertFalse(self.service._allow("127.0.0.2",False))

    def test_unavailable_database_reports_busy(self):
        class BusyDatabase:
            def execute(self,*args):
                raise sqlite3.OperationalError("database is locked")
        cookie=self.cookie()
        with patch.object(self.service,"db",BusyDatabase()):
            result=self.service.handle(self.header(2)+struct.pack("<IB",self.build,0)+cookie,self.addr)
            self.assertEqual((result[15],result[16]),(5,0))
            self.assertEqual(self.submit(),5)

    def test_lobby_routes_rankings_without_changing_rooms(self):
        spec = importlib.util.spec_from_file_location("leaderboard_lobby_test",ROOT/"server/lobby/lobby_server.py")
        lobby = importlib.util.module_from_spec(spec);spec.loader.exec_module(lobby)
        server = lobby.Server("127.0.0.1",0,self.path)
        try:
            sent = [];server.send=lambda packet,address:sent.append((packet,address))
            server.handle(self.header(1),self.addr)
            self.assertEqual(sent[-1][0][5],101)
            server.handle(self.header(1,2),self.addr)
            self.assertEqual(sent[-1][0][:6],b"H2RK\x02\x65")
            self.assertEqual(server.rooms,{})
            server.handle(b"H2LB\x01\x03"+struct.pack("<I",self.build),self.addr)
            self.assertEqual(sent[-1][0][:6],b"H2LB\x01\x66")
        finally:
            server.rankings.close();server.sock.close()


if __name__ == "__main__":
    unittest.main()
