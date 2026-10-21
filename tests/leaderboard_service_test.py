"""Local-only checks for opt-in unverified ranking storage and UDP protocol."""
import importlib.util
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

    def header(self,kind):
        self.serial += 1
        return rankings.MAGIC+bytes([kind])+struct.pack("<I",self.serial)

    def cookie(self,address=None):
        response = self.service.handle(self.header(1),address or self.addr)
        self.assertEqual(len(response),26)
        return response[10:26]

    def submit(self,table=0,score=100,rounds=0,difficulty=2,players=1,name=b"AMS",run=None,address=None,build=None):
        address = address or self.addr
        body = struct.pack("<IB3sIHBB16s",build or self.build,table,name,score,rounds,difficulty,players,run or secrets.token_bytes(16))
        response = self.service.handle(self.header(3)+body+self.cookie(address),address)
        self.assertEqual(len(response),11)
        return response[-1]

    def query(self,table=0,build=None):
        response = self.service.handle(self.header(2)+struct.pack("<IB",build or self.build,table)+self.cookie(),self.addr)
        self.assertLessEqual(len(response),127)
        self.assertEqual(response[15],0)
        return [rankings.ROW.unpack_from(response,17+i*11) for i in range(response[16])]

    def refill(self):
        self.service.budgets.clear()

    def test_round_trip_and_persistence(self):
        self.assertEqual(self.submit(score=900),0)
        self.assertEqual(self.query()[0],(b"AMS",900,0,2,1))
        self.service.close()
        self.service = rankings.Rankings(self.path)
        self.assertEqual(self.query()[0][1],900)

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
            self.assertEqual(server.rooms,{})
            server.handle(b"H2LB\x01\x03"+struct.pack("<I",self.build),self.addr)
            self.assertEqual(sent[-1][0][:6],b"H2LB\x01\x66")
        finally:
            server.rankings.close();server.sock.close()


if __name__ == "__main__":
    unittest.main()
