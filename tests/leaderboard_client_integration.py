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
                if packet[:6]==b"H2RK\x01\x01" and len(packet)==10:
                    wrong_request=struct.pack("<I",(struct.unpack_from("<I",packet,6)[0]+1)&0xffffffff)
                    server.send(b"H2RK\x01\x65"+wrong_request+bytes(16),address)
                if packet[:6]==b"H2RK\x01\x02" and len(packet)==31:
                    # Matching request/build but impossible row count must be ignored.
                    server.send(b"H2RK\x01\x66"+packet[6:15]+bytes([0,255]),address)
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
