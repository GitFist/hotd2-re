"""Loopback-only Typing input and recovery transport; no game assets or public services."""
import importlib.util
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("typing_network_lobby",ROOT/"server/lobby/lobby_server.py")
lobby=importlib.util.module_from_spec(spec);spec.loader.exec_module(lobby)
if len(sys.argv)!=2:
    raise SystemExit("usage: typing_network_integration.py <typing_network_test.exe>")
ready=threading.Event();stop=threading.Event();ports=[];errors=[]
observed={"input":0,"history":0,"dropped":0,"delayed":0}
pending=[]

def serve():
    server=None
    try:
        server=lobby.Server("127.0.0.1",0);server.sock.settimeout(.01)
        send=server.send
        def inspect(data,address):
            if data[:6]==b"H2LB\x01\x6a" and len(data)>=32:
                packet=data[26:]
                if packet[:4]==b"H2NP" and packet[4]==3:
                    observed["input"]+=1
                    assert len(packet)==29+packet[28]*49,"typing frame record is not 49 bytes"
                    if observed["dropped"]<2:
                        observed["dropped"]+=1;return
                elif packet[:4]==b"H2NP" and packet[4]==10:
                    observed["history"]+=1
                    assert len(packet)==19+packet[18]*102,"typing history does not contain two 49-byte records plus hash"
                    assert packet[18]<=10,"typing history packet exceeds bounded frame count"
                    if not observed["delayed"]:
                        observed["delayed"]=1;pending.append((time.monotonic()+.08,data,address));return
            send(data,address)
        server.send=inspect
        ports.append(server.sock.getsockname()[1]);ready.set()
        while not stop.is_set():
            for item in pending[:]:
                if item[0]<=time.monotonic():
                    send(item[1],item[2]);pending.remove(item)
            try: packet,address=server.sock.recvfrom(65536)
            except socket.timeout: continue
            except ConnectionResetError: continue
            server.handle(packet,address)
    except BaseException as error:
        errors.append(error);ready.set()
    finally:
        if server: server.sock.close()

thread=threading.Thread(target=serve);thread.start();ready.wait(3)
try:
    if errors: raise errors[0]
    if not ports: raise RuntimeError("local Typing service did not start")
    with tempfile.TemporaryDirectory(prefix="hotd2-typing-net-") as folder:
        env=os.environ.copy();env["HOTD2_NET_NODIRECT"]="1"
        result=subprocess.run([sys.argv[1],str(ports[0]),str(Path(folder)/"ticket.bin")],env=env,timeout=25)
        if errors: raise errors[0]
        if result.returncode: raise SystemExit(result.returncode)
    assert observed["input"] and observed["history"] and observed["dropped"]==2 and observed["delayed"]==1,observed
    print("PASS wire audit: 49-byte typing input, 102-byte history frames; input loss and delayed recovery exercised")
finally:
    stop.set();thread.join(3)
