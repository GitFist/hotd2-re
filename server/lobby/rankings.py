"""Optional client-reported leaderboards sharing the lobby's UDP socket.

H2RK v1/v2 is independent of lockstep. A return-routability cookie is not a player
identity or an anti-cheat check. Display names are not unique accounts; all results
must be labelled unverified. No source IP or private room token is persisted.
"""
import hashlib
import hmac
import secrets
import sqlite3
import struct
import time

MAGIC = b"H2RK\x01"
MAGIC_V2 = b"H2RK\x02"
MAX_ROWS_PER_BOARD = 1000
MAX_BUILDS = 32
MAX_TRACKED_IPS = 2048
MAX_PACKET = 128
COOKIE_SECONDS = 300
ROW = struct.Struct("<3sIHBB")  # initials, score/60-Hz time, rounds, difficulty/stage, players
ROW_V2 = struct.Struct("<12sIHBB")
SUBMIT = struct.Struct("<IB3sIHBB16s")
SUBMIT_V2 = struct.Struct("<IB12sIHBB16s")
SCHEMA = """CREATE TABLE IF NOT EXISTS rankings_v1 (
    run BLOB PRIMARY KEY CHECK(length(run)=16), build INTEGER NOT NULL,
    board INTEGER NOT NULL CHECK(board BETWEEN 0 AND 12),
    name BLOB NOT NULL CHECK(length(name) BETWEEN 1 AND 12),
    score INTEGER NOT NULL CHECK(score BETWEEN 1 AND 2000000000),
    rounds INTEGER NOT NULL CHECK(rounds BETWEEN 0 AND 65535),
    difficulty INTEGER NOT NULL CHECK(difficulty BETWEEN 0 AND 6),
    players INTEGER NOT NULL CHECK(players BETWEEN 1 AND 2),
    created INTEGER NOT NULL)"""


class Rankings:
    def __init__(self, database=None):
        self.secret = secrets.token_bytes(32)
        self.budgets = {}
        self.db = None
        if database:
            self.db = sqlite3.connect(database, timeout=0.1)
            self.db.execute("PRAGMA journal_mode=WAL")
            self.db.execute("PRAGMA synchronous=FULL")
            # SQLite cannot widen a CHECK in place. Rebuild an existing three-byte
            # table in one explicit transaction; run IDs, timestamps and rankings
            # survive, and an interrupted/failed migration rolls back the rename.
            try:
                with self.db:
                    self.db.execute("BEGIN IMMEDIATE")
                    self.db.execute(SCHEMA)
                    schema = self.db.execute("SELECT sql FROM sqlite_master WHERE type='table' AND name='rankings_v1'").fetchone()[0]
                    if "length(name)=3" in "".join(schema.lower().split()):
                        self.db.execute("ALTER TABLE rankings_v1 RENAME TO rankings_name3_legacy")
                        self.db.execute(SCHEMA)
                        self.db.execute("INSERT INTO rankings_v1 SELECT * FROM rankings_name3_legacy")
                        self.db.execute("DROP TABLE rankings_name3_legacy")
                    self.db.execute("CREATE INDEX IF NOT EXISTS rankings_board ON rankings_v1(build,board)")
            except sqlite3.Error:
                self.db.close()
                self.db = None
                raise

    def close(self):
        if self.db:
            self.db.close()
            self.db = None

    def _cookie(self, address, bucket):
        # Bind both port and address: a spoofed UDP request cannot retrieve a board
        # or upload a score without receiving a fresh cookie at that endpoint.
        source = (address[0] + ":" + str(address[1]) + ":" + str(bucket)).encode("ascii")
        return hmac.new(self.secret, source, hashlib.sha256).digest()[:16]

    def _cookie_valid(self, address, cookie):
        bucket = int(time.time()) // COOKIE_SECONDS
        return any(hmac.compare_digest(cookie, self._cookie(address, b)) for b in (bucket,bucket-1))

    def _allow(self, ip, submit):
        now = time.monotonic()
        if ip not in self.budgets:
            if len(self.budgets) >= MAX_TRACKED_IPS:
                self.budgets = {k:v for k,v in self.budgets.items() if now-v[2] < 300}
            if len(self.budgets) >= MAX_TRACKED_IPS:
                return False
            self.budgets[ip] = [20.0,2.0,now]
        query, write, previous = self.budgets[ip]
        query = min(20.0,query+(now-previous)*4)
        write = min(2.0,write+(now-previous)/10)
        allowed = write >= 1 if submit else query >= 1
        self.budgets[ip] = [query-(allowed and not submit),write-(allowed and submit),now]
        return allowed

    @staticmethod
    def _order(board):
        return "rounds DESC,score DESC,created,run" if board==2 else "score ASC,created,run" if board>=3 else "score DESC,created,run"

    def _query(self, build, board):
        if not self.db:
            return []
        return self.db.execute("SELECT name,score,rounds,difficulty,players FROM rankings_v1 WHERE build=? AND board=? ORDER BY " + self._order(board) + " LIMIT 10",(build,board)).fetchall()

    def _submit(self, build, board, name, score, rounds, difficulty, players, run):
        if not self.db:
            return 3  # service disabled until explicitly configured
        if (not build or board>12 or not 1<=score<=2000000000 or not 1<=players<=2 or
            difficulty>(6 if board<2 else 4) or not any(run) or
            not 1<=len(name)<=12 or
            any(c not in b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-" for c in name) or not name.strip() or
            (board!=2 and rounds) or (board==2 and not rounds) or (board>=3 and (players!=1 or score>1296000))):
            return 2
        values = (build,board,name,score,rounds,difficulty,players)
        try:
            with self.db:
                old = self.db.execute("SELECT build,board,name,score,rounds,difficulty,players FROM rankings_v1 WHERE run=?",(run,)).fetchone()
                if old:
                    return 1 if old==values else 2
                builds = self.db.execute("SELECT COUNT(DISTINCT build) FROM rankings_v1").fetchone()[0]
                if builds>=MAX_BUILDS and not self.db.execute("SELECT 1 FROM rankings_v1 WHERE build=? LIMIT 1",(build,)).fetchone():
                    return 5
                self.db.execute("INSERT INTO rankings_v1 VALUES (?,?,?,?,?,?,?,?,?)",(run,*values,int(time.time())))
                self.db.execute("DELETE FROM rankings_v1 WHERE build=? AND board=? AND run NOT IN (SELECT run FROM rankings_v1 WHERE build=? AND board=? ORDER BY " + self._order(board) + " LIMIT ?)",(build,board,build,board,MAX_ROWS_PER_BOARD))
                retained = self.db.execute("SELECT 1 FROM rankings_v1 WHERE run=?",(run,)).fetchone()
                return 0 if retained else 4
        except sqlite3.Error:
            return 5

    def handle(self, packet, address):
        """Return at most 127 (v1) / 217 (v2) bytes or None. Never forwards gameplay packets."""
        if not 10<=len(packet)<=MAX_PACKET or packet[:5] not in (MAGIC,MAGIC_V2):
            return None
        magic = packet[:5]
        modern = magic==MAGIC_V2
        kind = packet[5]
        request = packet[6:10]
        if kind==1 and len(packet)==10:
            if not self._allow(address[0],False): return None
            cookie = self._cookie(address,int(time.time())//COOKIE_SECONDS)
            return magic+bytes([101])+request+cookie
        if kind==2 and len(packet)==31:
            build,board = struct.unpack_from("<IB",packet,10)
            if not self._cookie_valid(address,packet[15:31]) or not self._allow(address[0],False): return None
            if not build or board>12: return None
            try:
                rows = self._query(build,board)
                status = 0 if self.db else 3
            except sqlite3.Error:
                rows,status = [],5
            # Status distinguishes an empty enabled board from an undeployed service.
            out = magic+bytes([102])+request+struct.pack("<IBBB",build,board,status,len(rows))
            if modern:
                return out+b"".join(ROW_V2.pack(*row) for row in rows)
            # Old clients still require three nonblank ASCII bytes. Only their
            # display is abbreviated; storage and v2 retain the complete name.
            def legacy(row):
                name = row[0][:3]
                if not name.strip(): name = row[0].lstrip()[:3]
                return ROW.pack(name.ljust(3,b" "),*row[1:])
            return out+b"".join(legacy(row) for row in rows)
        submit = SUBMIT_V2 if modern else SUBMIT
        cookie_start = 10+submit.size
        if kind==3 and len(packet)==cookie_start+16:
            if not self._cookie_valid(address,packet[cookie_start:]): return None
            if not self._allow(address[0],False): return None
            build,board,name,score,rounds,difficulty,players,run = submit.unpack_from(packet,10)
            if modern: name = name.rstrip(b"\0")  # only trailing NUL padding; embedded NUL remains invalid
            # Lost acknowledgements do not consume another write token. Repeated
            # datagrams still use the bounded read budget and cannot mutate a run.
            try:
                old = self.db.execute("SELECT build,board,name,score,rounds,difficulty,players FROM rankings_v1 WHERE run=?",(run,)).fetchone() if self.db else None
            except sqlite3.Error:
                return magic+bytes([103])+request+bytes([5])
            if old:
                status = 1 if old==(build,board,name,score,rounds,difficulty,players) else 2
            elif not self._allow(address[0],True):
                status = 5
            else:
                status = self._submit(build,board,name,score,rounds,difficulty,players,run)
            return magic+bytes([103])+request+bytes([status])
        return None
