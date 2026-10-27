"""Real-client rejoin regression: close/crash either player, keep playing, reopen and restore its slot.

Only owns processes it launches, with isolated folders and an ephemeral local lobby.
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import threading
import time

from netplay_relay_smoke import REPO, lobby, script


def rejoin_script():
    events = [(100, 1), (104, 0), (200, 4), (204, 0)]
    for frame in range(320, 365, 8):
        events += [(frame, 16), (frame + 4, 0)]
    events += [(400, 1), (404, 0), (450, 16), (454, 0), (458, 16), (462, 0),
               (466, 16), (470, 0), (490, 1), (494, 0)]
    lines = []
    for frame, buttons in events:
        # Keep the parked pointer outside rows while navigating, then shoot the
        # visible Rejoin row (the fourth lobby entry) to confirm it.
        x, y = (.5, 232 / 480) if frame >= 490 else (.1, .1)
        lines.extend(f'{frame} {p} {x} {y} {buttons}' for p in (0, 1))
    return '\n'.join(lines) + '\n'


def run(exe, departed, relay, crash=False, loss=False, outage=False, live_lobby=None, game=None):
    game = game or REPO / 'game'
    output = Path(tempfile.mkdtemp(prefix=f'hotd2_rejoin_{departed}_'))
    print(f'rejoin {departed}, relay={relay}, crash={crash}, loss={loss}: {output}', flush=True)
    server = lobby.Server('127.0.0.1', 0)
    stop = threading.Event()
    counts = {'dropped': 0, 'history': 0, 'tail_drops': 0, 'stale_packets': 0}
    blackout = [None]
    handover = {'frame': None, 'request': None}
    send = server.send

    def lossy_send(data, addr):
        if len(data) > 44 and data[5] == 106:
            if data[30] == 11 and data[36] == 1:
                handover['frame'] = struct.unpack_from('<I', data, 37)[0]
            if loss and data[30] == 10 and handover['frame'] and counts['tail_drops'] < 3:
                first, count = struct.unpack_from('<I', data, 40)[0], data[44]
                final_history = handover['frame'] - 1
                if first <= final_history < first + count:
                    # Deliver through the penultimate frame, lose the final frame even after the
                    # survivor resumes. It must keep answering history requests after handover.
                    prefix = final_history - first
                    send(data[:44] + bytes([prefix]) + data[45:45 + prefix * 32], addr)
                    counts['tail_drops'] += 1
                    return
            if loss and data[30] == 3 and handover['request'] and handover['frame']:
                frame = struct.unpack_from('<I', data, 42)[0]
                if handover['frame'] + 30 <= frame < 10000 and not counts['stale_packets']:
                    old, target = handover['request']
                    send(old, target)
                    send(old[:26] + b'H2NP\x04\x03' + struct.pack('<I', 0), target)
                    counts['stale_packets'] = 2
        if len(data) >= 40 and data[5] == 106 and data[30] == 7:
            handover['request'] = (data, addr)
        if outage and len(data) >= 46 and data[5] == 106 and data[30] == 3:
            frame = struct.unpack_from('<I', data, 42)[0]
            if blackout[0] is None and 2500 <= frame < 10000:
                blackout[0] = time.monotonic()
        if outage and blackout[0] is not None and time.monotonic() - blackout[0] < 7:
            counts['dropped'] += 1
            return
        if len(data) > 31 and data[5] == 106 and data[30] >= 7:
            counts['history'] += 1
            if loss and counts['history'] % 11 == 0:
                counts['dropped'] += 1
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
                pass
    worker = threading.Thread(target=service, daemon=True)
    worker.start()
    processes, logs = [], []
    endpoint = live_lobby or f'127.0.0.1:{server.sock.getsockname()[1]}'
    folders = {role: output / role for role in ('host', 'join')}
    survivor = 'join' if departed == 'host' else 'host'

    def launch(role, returning=False):
        folder = folders[role]
        folder.mkdir(exist_ok=True)
        label = role + ('-return' if returning else '')
        inputs = folder / (label + '.txt')
        # These recorded navigation events target the Classic front end. Pin the
        # theme explicitly instead of depending on the client's current default.
        settings = folder / 'smoke-settings.ini'
        settings.write_text('menu=classic\n')
        text = rejoin_script() if returning else script('public', role == 'host')
        if not returning:
            # Confirm the visible lobby rows. A shot in the padding above the
            # text no longer activates the keyboard-selected row.
            aim = {1000: 142, 1004: 142, 1080: 202, 1084: 202} if role == 'host' else {
                1040: 172, 1044: 172, 1200: 104, 1204: 104}
            lines = []
            for line in text.splitlines():
                values = line.split()
                if int(values[0]) in aim:
                    values[3] = str(aim[int(values[0])] / 480)
                lines.append(' '.join(values))
            text = '\n'.join(lines) + '\n'
        # Aim, fire, reload and pause/resume after the rejoin; both copies must stay synchronized.
        for frame, buttons in ((4000, 1), (4001, 0), (4050, 2), (4051, 0), (4140, 4), (4141, 0), (4160, 4), (4161, 0)):
            text += f'{frame} {0 if role == "host" else 1} {.2 if role == "host" else .8} .7 {buttons}\n'
        inputs.write_text(text)
        env = {k: v for k, v in os.environ.items() if not k.startswith('HOTD2_')}
        env.update(HOTD2_PLAY_MENU='1', HOTD2_PLAY_AIMLOG='1', HOTD2_NET_LOG='1', HOTD2_PLAY_ZLOG=str(folder / (label + '.zlog')))
        if relay or outage:
            env['HOTD2_NET_NODIRECT'] = '1'
        log = (output / (label + '.log')).open('w')
        logs.append(log)
        final = '2600' if role == departed and not returning and not crash and not outage else '4300'
        args = [str(exe), str(game), '--scene', str(REPO / 'scenes/st1_play.scene'),
                '--script', str(inputs), '--pace', '--trace', '--screenshot', str(folder / (label + '-%d.png')),
                '--every', '200', '--every-from', '2400', '--frame', final, '--port', '0', '--lobby', endpoint,
                '--width', '640', '--height', '480', '--settings', str(settings)]
        p = subprocess.Popen(args, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                             creationflags=subprocess.CREATE_NO_WINDOW)
        processes.append(p)
        return p

    def wait_for(test, seconds, reason):
        deadline = time.monotonic() + seconds
        while not test():
            if time.monotonic() > deadline:
                raise TimeoutError(f'{reason}: {output}')
            time.sleep(.1)

    try:
        initial = {r: launch(r) for r in ('host', 'join')}
        if outage:
            returning = initial[departed]
        elif crash:
            wait_for(lambda: (folders[departed] / f'{departed}-2600.png').exists(), 75, 'first gameplay checkpoint')
            initial[departed].kill()
            initial[departed].wait()
        else:
            wait_for(lambda: initial[departed].poll() is not None, 75, 'departing client exit')
            assert initial[departed].returncode == 0
        if not outage:
            wait_for(lambda: (folders[survivor] / f'{survivor}-2800.png').exists(), 20, 'survivor must keep playing')
            assert (folders[departed] / 'hotd2re_rejoin.bin').stat().st_size == 46
            if not live_lobby:
                room = next(iter(server.rooms.values()))
                old_endpoint = room.host_public if departed == 'host' else room.joiner_public
            returning = launch(departed, True)
        wait_for(lambda: all(p.poll() is not None for p in (initial[survivor], returning)), 90, 'rejoin and finish')
        assert initial[survivor].returncode == returning.returncode == 0
        if not outage and not live_lobby:
            new_endpoint = room.host_public if departed == 'host' else room.joiner_public
            assert new_endpoint != old_endpoint, (old_endpoint, new_endpoint)
        elif outage:
            assert counts['dropped'] > 0
        hashes = []
        suffix = '' if outage else '-return'
        for role, label_suffix in ((survivor, ''), (departed, suffix)):
            text = (output / f'{role}{label_suffix}.log').read_text()
            player = 1 if role == 'host' else 2
            assert f'rejoined as P{player}' in text, text[-5000:]
            assert 'DESYNC' not in text and 'rejoin failed' not in text and 'discontinuity' not in text, text[-5000:]
            # The original pause overlay deliberately submits no stage geometry.
            assert 'stage-models 0' not in text or 'paused 1' in text, text[-2000:]
            assert 'frame 4300 mode 6' in text, text[-2000:]
            hashes.append(re.search(r'final state hash ([A-F0-9]+) at frame 4300', text)[1])
        assert hashes[0] == hashes[1], hashes
        if loss:
            assert counts['tail_drops'] == 3 and counts['stale_packets'] == 2, counts
        a = (folders[survivor] / f'{survivor}.zlog').read_text().splitlines()
        b = (folders[departed] / f'{departed}{suffix}.zlog').read_text().splitlines()
        by_frame = lambda lines: {int(line.split()[0]): line for line in lines if line and line[0].isdigit()}
        a, b = by_frame(a), by_frame(b)
        for frame in range(4200, 4301):
            assert a[frame] == b[frame], (frame, a[frame], b[frame])
        report = dict(passed=True, departed=departed, relay=relay, crash=crash, loss=loss, outage=outage, live_lobby=live_lobby, counts=counts, output=str(output))
        (output / 'verification.json').write_text(json.dumps(report, indent=2))
        print(f'PASS {departed}: survivor advanced alone, endpoint changed, slot retained, final world hashes match', flush=True)
        return report
    finally:
        for p in processes:
            if p.poll() is None:
                p.kill(); p.wait()
        for log in logs:
            log.close()
        stop.set(); worker.join(timeout=2); server.sock.close()


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=REPO / 'build/src/port/Release/play.exe')
    ap.add_argument('--game', type=Path, default=REPO / 'game', help='original game data directory')
    ap.add_argument('--departed', choices=['host', 'join'], default='join')
    ap.add_argument('--relay', action='store_true')
    ap.add_argument('--crash', action='store_true')
    ap.add_argument('--loss', action='store_true')
    ap.add_argument('--outage', action='store_true', help='seven-second network outage; recover without restarting either client')
    ap.add_argument('--lobby', help='use a deployed lobby (without local packet loss/outage injection)')
    args = ap.parse_args()
    if args.lobby and (args.loss or args.outage):
        ap.error('loss/outage injection requires the isolated local server')
    run(args.exe.resolve(), args.departed, args.relay, args.crash, args.loss, args.outage, args.lobby, args.game.resolve())
