"""Fault-injected loopback-only lobby lifecycle test; no public service used."""
import importlib.util
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("lifecycle_lobby",ROOT/"server/lobby/lobby_server.py")
lobby=importlib.util.module_from_spec(spec);spec.loader.exec_module(lobby)
if len(sys.argv)!=2:
    raise SystemExit("usage: lobby_lifecycle_integration.py <lobby_lifecycle_test.exe>")
ready=threading.Event();stop=threading.Event();ports=[];errors=[]
creates=[];cancelled=set();traffic=[];dropped=[];delayed=[];pending=[];list_count=0;drop_until={}
silent=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);silent.bind(("127.0.0.1",0))

def serve():
    global list_count
    server=None
    try:
        server=lobby.Server("127.0.0.1",0);server.sock.settimeout(.01)
        send=server.send
        def faulty_send(data,address):
            if data[:6]==b"H2LB\x01\x65" and len(creates)==2 and address==creates[1]:
                pending.append((time.monotonic()+.35,data,address));delayed.append("CREATED");return
            if data[:6]==b"H2LB\x01\x66" and list_count==2:
                pending.append((time.monotonic()+.75,data,address));delayed.append("LIST");return
            send(data,address)
        server.send=faulty_send
        ports.append(server.sock.getsockname()[1]);ready.set()
        while not stop.is_set():
            now=time.monotonic()
            for item in pending[:]:
                if item[0]<=now:
                    send(item[1],item[2]);pending.remove(item)
            try: packet,address=server.sock.recvfrom(65536)
            except socket.timeout: continue
            except ConnectionResetError: continue
            if packet[:5]==b"H2LB\x01" and len(packet)>=6:
                kind=packet[5];traffic.append((kind,address))
                if kind==1 and address not in creates: creates.append(address)
                if kind==3: list_count+=1
                if kind==5:
                    cancelled.add(address)
                    if address==creates[0]:
                        deadline=drop_until.setdefault(address,time.monotonic()+.3)
                        if time.monotonic()<deadline:
                            dropped.append(address);continue
            server.handle(packet,address)
    except BaseException as error:
        errors.append(error);ready.set()
    finally:
        if server: server.sock.close()

thread=threading.Thread(target=serve);thread.start();ready.wait(3)
try:
    if errors: raise errors[0]
    if not ports: raise RuntimeError("local lifecycle service did not start")
    result=subprocess.run([sys.argv[1],str(ports[0]),str(silent.getsockname()[1])],timeout=40)
    if errors: raise errors[0]
    if result.returncode: raise SystemExit(result.returncode)
    if len(creates)!=3: raise AssertionError(f"expected three explicit CREATE endpoints, saw {len(creates)}")
    if len(dropped)<3 or "CREATED" not in delayed or "LIST" not in delayed:
        raise AssertionError("required loss/reordering faults did not execute")
    for address in creates[:2]:
        if sum(kind==1 and endpoint==address for kind,endpoint in traffic)!=1:
            raise AssertionError("cancelled room issued another CREATE")
        if any(kind in (2,6) and endpoint==address for kind,endpoint in traffic):
            raise AssertionError("cancelled room emitted refresh/relay keepalives")
        if not any(kind==5 and endpoint==address for kind,endpoint in traffic):
            raise AssertionError("cancelled endpoint never sent authenticated LEAVE")
    print("PASS wire audit: original server protocol, bounded cancellation-only sockets; delayed CREATED/LIST and lost LEAVE exercised")
finally:
    stop.set();thread.join(3);silent.close()
