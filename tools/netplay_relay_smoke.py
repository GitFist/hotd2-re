"""Exercise the actual Windows clients through a local relay, including packet loss.

Run from the repository: python tools/netplay_relay_smoke.py [public|private|p2p ...]
Uses temporary output folders, headless rendering and fresh saves; never edits game assets.
"""
import importlib.util
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("hotd2_lobby", REPO / "server/lobby/lobby_server.py")
lobby = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lobby)


def script(mode, host):
    events = [(700, 4), (712, 0)]
    for frame in range(820, 865, 8):
        events += [(frame, 16), (frame + 4, 0)]
    events += [(900, 1), (904, 0)]
    if mode == "p2p":
        events += [(1000, 16), (1004, 0), (1008, 16), (1012, 0), (1040, 1), (1044, 0)]
        if not host:
            events += [(1080, 16), (1084, 0)]
        events += [(1120, 1), (1124, 0)]
    elif host:
        events += [(1000, 1), (1004, 0), (1040, 16), (1044, 0)]
        if mode == "private":
            events += [(1048, 64), (1052, 0), (1056, 16), (1060, 0)]
        else:
            events += [(1048, 16), (1052, 0)]
        events += [(1080, 1), (1084, 0)]
    else:
        events += [(1000, 16), (1004, 0), (1040, 1), (1044, 0), (1200, 1), (1204, 0)]
    # The host spends ~2300 boot frames waiting when we start the joiner after reading its code.
    # Keep reload/back inputs outside that window, including the deliberately delayed handshake.
    for frame in range(2600, 3000, 24):
        events += [(frame, 2 if frame % 120 == 20 else 1), (frame + 4, 0)]
    return "".join(f"{frame} {player} 0.5 0.25 {buttons}\n" for frame, buttons in events for player in (0, 1))


def run(mode):
    output = Path(tempfile.mkdtemp(prefix=f"hotd2_relay_{mode}_"))
    server = lobby.Server("127.0.0.1", 0)
    stop = threading.Event()
    counts = {"forwarded": 0, "dropped": 0, "bytes": 0}
    dropped = set()
    send = server.send

    def lossy_send(data, addr):
        if data[5] == 106:
            counts["forwarded"] += 1
            counts["bytes"] += len(data)
            kind = data[30]
            # Lose each side's first hello/welcome and occasional inputs; retransmission must recover.
            key = (addr, kind)
            if (kind in (1, 2) and key not in dropped) or (kind == 3 and counts["forwarded"] % 31 == 0):
                dropped.add(key)
                counts["dropped"] += 1
                return
        send(data, addr)

    server.send = lossy_send

    def service():
        while not stop.is_set():
            try:
                data, addr = server.sock.recvfrom(65536)
                server.handle(data, addr)
            except socket.timeout:
                server.expire()
            except ConnectionResetError:
                continue  # Windows reports ICMP when the other test client has already exited

    worker = threading.Thread(target=service, daemon=True)
    worker.start()
    processes, logs = [], []
    env = {k: v for k, v in os.environ.items() if not k.startswith("HOTD2_")}
    env["HOTD2_PLAY_MENU"] = "1"
    env["HOTD2_NET_LOG"] = "1"
    env["HOTD2_NET_NODIRECT"] = "1"  # F otherwise bypasses the simulated relay loss with a direct connection
    endpoint = f"127.0.0.1:{server.sock.getsockname()[1]}"
    print(f"{mode}: testing relay with packet loss; results in {output}", flush=True)
    try:
        for role in ("host", "join"):
            folder = output / role
            folder.mkdir()
            inputs = folder / "input.txt"
            inputs.write_text(script(mode, role == "host"))
            if role == "join" and mode != "public":
                deadline = time.monotonic() + 30
                while True:
                    match = re.search(r"netplay: room \d+ code (\d+)", (output / "host.log").read_text())
                    if match:
                        env["HOTD2_NET_PEERCODE"] = match[1]
                        break
                    if time.monotonic() > deadline:
                        raise TimeoutError("Host did not create a room")
                    time.sleep(0.1)
            log = open(output / f"{role}.log", "w")
            logs.append(log)
            args = [str(REPO / "build/src/port/Release/hotd2.exe"), str(REPO / "game"),
                    "--scene", str(REPO / "scenes/st1_play.scene"), "--script", str(inputs),
                    "--pace", "--trace", "--screenshot", str(folder / "game.png"), "--frame", "3000",
                    "--port", "0", "--lobby", endpoint, "--width", "640", "--height", "480"]
            processes.append(subprocess.Popen(args, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                                              creationflags=subprocess.CREATE_NO_WINDOW))
        deadline = time.monotonic() + 85
        while any(p.poll() is None for p in processes):
            if time.monotonic() > deadline:
                raise TimeoutError("Clients did not finish the relay game")
            time.sleep(0.2)
        assert all(p.returncode == 0 for p in processes), [p.returncode for p in processes]
    finally:
        for p in processes:
            if p.poll() is None:
                p.kill()
                p.wait()
        for log in logs:
            log.close()
        stop.set()
        worker.join(timeout=2)
        server.sock.close()
    results = {}
    for role in ("host", "join"):
        text = (output / f"{role}.log").read_text()
        assert f'connected as P{1 if role == "host" else 2} (lobby)' in text, text[-3000:]
        assert "via server relay" in text and "frame 3000 mode 6" in text, text[-3000:]
        assert "DESYNC" not in text and "continuing alone" not in text, text[-3000:]
        results[role] = [line for line in text.splitlines() if "connected as" in line or "frame 3000" in line]
    assert counts["forwarded"] > 1000 and counts["dropped"] > 10, counts
    report = dict(mode=mode, passed=True, output=str(output), **counts, results=results)
    (output / "results.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2), flush=True)
    return report


if __name__ == "__main__":
    for mode in sys.argv[1:] or ("public", "private", "p2p"):
        if mode not in ("public", "private", "p2p"):
            raise SystemExit("Expected public, private or p2p")
        run(mode)
