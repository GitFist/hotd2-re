"""Loopback-only Session regression for one-way direct paths and lost lobby replies."""
import argparse
import importlib.util
import os
from pathlib import Path
import select
import socket
import subprocess
import threading

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("hotd2_connection_lobby", ROOT / "server/lobby/lobby_server.py")
lobby = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lobby)
CASES = ("direct", "blocked", "host-to-guest", "guest-to-host", "large-welcome-loss", "initial-lobby-loss")


def run(executable, scenario):
    server = lobby.Server("127.0.0.1", 0)
    proxy_host = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    proxy_guest = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    proxy_host.bind(("127.0.0.1", 0))
    proxy_guest.bind(("127.0.0.1", 0))
    sockets = (server.sock, proxy_host, proxy_guest)
    for channel in sockets:
        channel.setblocking(False)
    actual = {}
    counts = {"host": 0, "guest": 0, "dropped_direct": 0, "relayed": 0, "joined_lost": 0, "peer_lost": 0}
    original_send = server.send

    def send(data, address):
        # Advertise only the proxy candidates. The server's real member endpoints,
        # tokens, authentication, retry behavior and relay implementation stay intact.
        if data[:4] == b"H2LB" and len(data) >= 6:
            kind = data[5]
            if kind == 103 and len(data) >= 23 and data[10] == 0:
                if scenario == "initial-lobby-loss" and not counts["joined_lost"]:
                    counts["joined_lost"] += 1
                    return
                endpoint = lobby.pack_endpoint(proxy_host.getsockname())
                data = data[:11] + endpoint * 2 + data[23:]
            elif kind == 104 and len(data) >= 18:
                if scenario == "initial-lobby-loss" and not counts["peer_lost"]:
                    counts["peer_lost"] += 1
                    return
                endpoint = lobby.pack_endpoint(proxy_guest.getsockname())
                data = data[:6] + endpoint * 2 + data[18:]
            elif kind == 106:
                # H2NP keepalive (type 6) maintains the standby relay even when
                # gameplay uses the healthy direct route exclusively.
                if len(data) >= 32 and data[26:30] == b"H2NP" and data[30] != 6:
                    counts["relayed"] += 1
        original_send(data, address)

    server.send = send
    stopped = threading.Event()
    failures = []

    def serve():
        try:
            while not stopped.is_set():
                for channel in select.select(sockets, [], [], .02)[0]:
                    try:
                        data, address = channel.recvfrom(65536)
                    except ConnectionResetError as error:
                        # Winsock reports a previous UDP send's ICMP unreachable
                        # here when a native peer closes; it is not a datagram.
                        if getattr(error, "winerror", None) == 10054:
                            continue
                        raise
                    if channel is server.sock:
                        if data[:4] == b"H2LB" and len(data) >= 6:
                            if data[5] == 1:
                                actual["host"] = address
                            elif data[5] == 4:
                                actual["guest"] = address
                        server.handle(data, address)
                        continue
                    # A host packet targets proxy_guest and arrives at the guest
                    # from proxy_host; the reverse direction mirrors that route.
                    from_host = channel is proxy_guest
                    sender, receiver = ("host", "guest") if from_host else ("guest", "host")
                    counts[sender] += 1
                    blocked = (scenario == "blocked" or
                               scenario == "host-to-guest" and not from_host or
                               scenario == "guest-to-host" and from_host or
                               scenario == "large-welcome-loss" and data[:4] == b"H2NP" and
                               len(data) > 1200 and data[4] == 2)
                    if blocked:
                        counts["dropped_direct"] += 1
                    elif receiver in actual:
                        outgoing = proxy_host if from_host else proxy_guest
                        outgoing.sendto(data, actual[receiver])
        except BaseException as error:
            failures.append(error)

    worker = threading.Thread(target=serve, daemon=True)
    worker.start()
    try:
        environment = os.environ.copy()
        for variable in ("HOTD2_NET_NODIRECT", "HOTD2_NET_NORELAY", "HOTD2_NET_CUTDIRECT", "HOTD2_NET_LOCALCODE"):
            environment.pop(variable, None)
        result = subprocess.run([str(executable), str(server.sock.getsockname()[1]), scenario],
                                capture_output=True, text=True, timeout=20, env=environment,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        if failures:
            raise RuntimeError(f"{scenario}: loopback harness failed") from failures[0]
        if result.returncode:
            raise AssertionError(f"{scenario}: native test exited {result.returncode}\n{result.stdout}{result.stderr}")
        assert counts["host"] and counts["guest"], (scenario, "both direct paths were not exercised", counts)
        if scenario in ("blocked", "host-to-guest", "guest-to-host", "large-welcome-loss"):
            assert counts["dropped_direct"] and counts["relayed"], (scenario, "fallback was not exercised", counts)
        if scenario == "direct":
            assert not counts["relayed"], ("healthy connection relayed gameplay data", counts)
        if scenario == "initial-lobby-loss":
            assert counts["joined_lost"] == counts["peer_lost"] == 1, counts
        print(result.stdout.strip(), flush=True)
    finally:
        stopped.set()
        worker.join(timeout=2)
        for channel in sockets:
            channel.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build-x64/Release/lobby_connection_test.exe")
    parser.add_argument("--cases", choices=CASES, nargs="+", default=CASES)
    arguments = parser.parse_args()
    for case in arguments.cases:
        run(arguments.exe.resolve(), case)
