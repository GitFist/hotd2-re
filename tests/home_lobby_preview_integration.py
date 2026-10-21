"""Run actual UDP Sessions against a loopback-only lobby; never contact the VPS."""
import importlib.util
from pathlib import Path
import socket
import subprocess
import sys
import threading

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("home_preview_lobby",ROOT/"server/lobby/lobby_server.py")
lobby=importlib.util.module_from_spec(spec);spec.loader.exec_module(lobby)
if len(sys.argv)!=3:
    raise SystemExit("usage: home_lobby_preview_integration.py <home_lobby_preview_test.exe> <game-dir>")
ready=threading.Event();stop=threading.Event();ports=[];errors=[];directory_endpoints=set();join_endpoints=[]
def serve():
    server=None
    try:
        server=lobby.Server("127.0.0.1",0);server.sock.settimeout(.1)
        ports.append(server.sock.getsockname()[1]);ready.set()
        while not stop.is_set():
            try: packet,address=server.sock.recvfrom(65536)
            except socket.timeout: continue
            except ConnectionResetError: continue  # retired UDP endpoints may reject a delayed response on Windows
            if packet[:5]==b"H2LB\x01" and len(packet)>=6:
                if packet[5]==3: directory_endpoints.add(address)
                elif packet[5]==4: join_endpoints.append(address)
            server.handle(packet,address)
    except BaseException as error:
        errors.append(error);ready.set()
    finally:
        if server: server.sock.close()
thread=threading.Thread(target=serve);thread.start();ready.wait(3)
try:
    if errors: raise errors[0]
    if not ports: raise RuntimeError("local preview service did not start")
    result=subprocess.run([sys.argv[1],str(ports[0]),sys.argv[2]],timeout=40)
    if errors: raise errors[0]
    if result.returncode: raise SystemExit(result.returncode)
    if len(join_endpoints)!=3: raise AssertionError(f"expected three deliberate JOINs (public,privatewrong,privatecorrect), saw {len(join_endpoints)}")
    if directory_endpoints.intersection(join_endpoints): raise AssertionError("Home read-only directory socket emitted a JOIN")
    print("PASS server packet audit: Home directory only lists, explicit join sockets perform three authorized test attempts")
finally:
    stop.set();thread.join(3)
