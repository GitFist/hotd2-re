"""Run a compiled leaderboard_client_test against a local temporary service only."""
import importlib.util
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("local_rankings_lobby",ROOT/"server/lobby/lobby_server.py")
lobby=importlib.util.module_from_spec(spec);spec.loader.exec_module(lobby)

if len(sys.argv)!=2:
    raise SystemExit("usage: leaderboard_client_integration.py <leaderboard_client_test.exe>")
with tempfile.TemporaryDirectory(prefix="hotd2-rank-client-") as tmp:
    # SQLite belongs to the receiving thread, as it does in the production service.
    ready=threading.Event();stop=threading.Event();port=[];errors=[]
    def serve():
        server=None
        try:
            server=lobby.Server("127.0.0.1",0,str(Path(tmp)/"records.sqlite3"))
            server.sock.settimeout(.1);port.append(server.sock.getsockname()[1]);ready.set()
            while not stop.is_set():
                try: packet,address=server.sock.recvfrom(65536)
                except socket.timeout: continue
                ranking_header=packet[:5] in (b"H2RK\x01",b"H2RK\x02")
                if ranking_header and packet[5:6]==b"\x01" and len(packet)==10:
                    # This fixture exercises independent names/runs in one burst;
                    # rate limiting has separate service tests.
                    server.rankings.budgets.clear()
                    wrong_request=struct.pack("<I",(struct.unpack_from("<I",packet,6)[0]+1)&0xffffffff)
                    server.send(packet[:5]+b"\x65"+wrong_request+bytes(16),address)
                    if packet[4]==2:
                        server.send(b"H2RK\x01\x65"+packet[6:10]+bytes(16),address)
                if ranking_header and packet[5:6]==b"\x02" and len(packet)==31:
                    # Matching request/build but impossible row count must be ignored.
                    server.send(packet[:5]+b"\x66"+packet[6:15]+bytes([0,255]),address)
                    if packet[4]==2:
                        result_header=packet[:5]+b"\x66"+packet[6:15]
                        # A terminator followed by nonzero bytes is not canonical name padding.
                        malformed=struct.pack("<12sIHBB",b"OK\x00BAD",123,0,1,1)
                        server.send(result_header+bytes([0,1])+malformed,address)
                        # A v1-width row is not a valid v2 response.
                        server.send(result_header+bytes([0,1])+struct.pack("<3sIHBB",b"OLD",123,0,1,1),address)
                        if packet[14]==9:
                            # Exercise all 217 bytes of the largest valid response on
                            # this local test socket. Other boards use the real service.
                            rows=b"".join(struct.pack("<12sIHBB",f"LONG_NAME-{index:02}".encode(),100+index,0,1,1) for index in range(10))
                            server.send(result_header+bytes([0,10])+rows,address)
                            continue
                server.handle(packet,address)
        except BaseException as error:
            errors.append(error);ready.set()
        finally:
            if server: server.rankings.close();server.sock.close()
    thread=threading.Thread(target=serve);thread.start();ready.wait(3)
    try:
        if errors: raise errors[0]
        if not port: raise RuntimeError("local test service did not start")
        result=subprocess.run([sys.argv[1],str(port[0])],timeout=30)
        if errors: raise errors[0]
        if result.returncode: raise SystemExit(result.returncode)
    finally:
        stop.set();thread.join(3)
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as blackhole:
    blackhole.bind(("127.0.0.1",0))
    result=subprocess.run([sys.argv[1],str(blackhole.getsockname()[1]),"unavailable"],timeout=10)
    if result.returncode: raise SystemExit(result.returncode)
