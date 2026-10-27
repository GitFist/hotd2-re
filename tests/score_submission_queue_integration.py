"""Exercise persistence/restart and response loss using only a temporary localhost service."""
import importlib.util
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("queue_rankings", ROOT / "server/lobby/rankings.py")
rankings = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rankings)

if len(sys.argv) != 2:
    raise SystemExit("usage: score_submission_queue_integration.py <score_submission_queue_test.exe>")

with tempfile.TemporaryDirectory(prefix="hotd2-score-sync-") as tmp:
    ready, stop, drop_ack = threading.Event(), threading.Event(), threading.Event()
    drop_ack.set()
    ports, errors, observed, rows = [], [], {}, []

    def serve():
        service = None
        try:
            service = rankings.Rankings(str(Path(tmp) / "records.sqlite3"))
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.bind(("127.0.0.1", 0))
                sock.settimeout(.1)
                ports.append(sock.getsockname()[1])
                ready.set()
                busy_once = True
                while not stop.is_set():
                    try:
                        packet, address = sock.recvfrom(1024)
                    except socket.timeout:
                        continue
                    if packet[:6] == b"H2RK\x02\x01":
                        service.budgets.clear()  # rate limiting is covered by service tests
                    response = None
                    name = b""
                    if packet[:6] == b"H2RK\x02\x03":
                        build, board, name, score, rounds, difficulty, players, run = rankings.SUBMIT_V2.unpack_from(packet, 10)
                        name = name.rstrip(b"\0")
                        observed.setdefault(name, []).append(run)
                        if name in (b"REJECTED", b"CUTOFF"):
                            status = 2 if name == b"REJECTED" else 4
                            response = packet[:5] + b"\x67" + packet[6:10] + bytes([status])
                        elif name == b"BUSY" and busy_once:
                            busy_once = False
                            response = packet[:5] + b"\x67" + packet[6:10] + b"\x05"
                    if response is None:
                        response = service.handle(packet, address)
                    # Commit the result, then discard every acknowledgement until
                    # the first client exits. The next process must reuse its ID.
                    if name == b"LOST_ACK" and drop_ack.is_set():
                        continue
                    if response is not None:
                        sock.sendto(response, address)
                rows.extend(service.db.execute("SELECT name,score,run FROM rankings_v1").fetchall())
        except BaseException as error:
            errors.append(error)
            ready.set()
        finally:
            if service:
                service.close()

    thread = threading.Thread(target=serve)
    thread.start()
    ready.wait(3)
    try:
        if errors:
            raise errors[0]
        if not ports:
            raise RuntimeError("local queue service did not start")
        for phase in ("lost", "resume", "terminal"):
            if phase == "resume":
                drop_ack.clear()
            subprocess.run([sys.argv[1], str(ports[0]), tmp, phase], check=True, timeout=15)
            if errors:
                raise errors[0]
    finally:
        stop.set()
        thread.join(3)
    if errors:
        raise errors[0]
    assert sorted((name, score) for name, score, run in rows) == [(b"BUSY", 333), (b"LOST_ACK", 111), (b"RENAMED", 222)], rows
    assert len(observed[b"LOST_ACK"]) >= 2 and len(set(observed[b"LOST_ACK"])) == 1, observed
    assert len(observed[b"BUSY"]) >= 2 and len(set(observed[b"BUSY"])) == 1, observed
    assert len(observed[b"REJECTED"]) == len(observed[b"CUTOFF"]) == 1, observed
    assert len({run for name, score, run in rows}) == 3, rows
    print("PASS queue integration: lost acknowledgement + process restart dedup, immutable rename, transient retry, durable terminal records")
