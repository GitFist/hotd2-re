"""Isolated real relay: desync notification survives invalidation, then Return ends the room."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
from netplay_relay_smoke import lobby


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("game", type=Path)
    args = parser.parse_args()
    server = lobby.Server("127.0.0.1", 0)
    stopped = threading.Event()
    ended = []
    handle = server.handle

    def tracked_handle(data, address):
        if data[:6] == b"H2LB\x01\x08":
            ended.append(True)
        handle(data, address)

    server.handle = tracked_handle

    def service():
        while not stopped.is_set():
            try:
                data, address = server.sock.recvfrom(65536)
                server.handle(data, address)
            except TimeoutError:
                pass
            except ConnectionResetError:
                pass

    worker = threading.Thread(target=service, daemon=True)
    worker.start()
    try:
        env = {key: value for key, value in os.environ.items() if not key.startswith("HOTD2_")}
        env["HOTD2_NET_NODIRECT"] = "1"
        result = subprocess.run([str(args.exe.resolve()), str(args.game.resolve()),
                                 str(server.sock.getsockname()[1])], env=env, capture_output=True,
                                text=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
        assert result.returncode == 0, "relay desync regression failed"
        # Closing a socket alone can also remove a room. Require the explicit
        # END request from Return after replay history was already invalidated.
        deadline = time.monotonic() + 2
        while not ended and time.monotonic() < deadline:
            time.sleep(.01)
        assert ended, "Return did not end the invalidated rejoinable match"
        print("PASS relay stayed available for both desync notices; explicit Return ended the invalidated room")
    finally:
        stopped.set()
        worker.join(timeout=2)
        server.sock.close()


if __name__ == "__main__":
    main()
