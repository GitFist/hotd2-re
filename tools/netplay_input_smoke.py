"""Check real clients' remote crosshair sprites and player-slot ownership after disconnect.

Uses an isolated local lobby and temporary client folders. No running user game is touched.
Examples: python tools/netplay_input_smoke.py --cases aim-direct aim-relay host-leaves join-leaves
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import threading
import time

from netplay_relay_smoke import REPO, lobby, script


def moving_script(host):
    # Keep the existing menu flow, then move each player's cursor to distinct positions twice.
    # Each checkpoint follows a long hold, so the local cursor and the delayed remote one agree.
    lines = [line for line in script('public', host).splitlines() if int(line.split()[0]) < 2500]
    for frame, aims in ((2500, ((.2, .3), (.8, .7))), (2850, ((.65, .35), (.25, .6)))):
        for player, (x, y) in enumerate(aims):
            lines.append(f'{frame} {player} {x} {y} 0')
    return '\n'.join(lines) + '\n'


def run(case, exe):
    output = Path(tempfile.mkdtemp(prefix=f'hotd2_input_{case}_'))
    server = lobby.Server('127.0.0.1', 0)
    stop = threading.Event()

    def service():
        while not stop.is_set():
            try:
                data, addr = server.sock.recvfrom(65536)
                server.handle(data, addr)
            except socket.timeout:
                server.expire()
            except ConnectionResetError:
                continue

    worker = threading.Thread(target=service, daemon=True)
    worker.start()
    port_guard = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        port_guard.bind(('0.0.0.0', 27015))
    except OSError:
        port_guard.close()
        port_guard = None
    early = {'host-leaves': 'host', 'join-leaves': 'join'}.get(case)
    procs, logs = {}, []
    print(f'{case}: {output}', flush=True)
    try:
        for role in ('host', 'join'):
            folder = output / role
            folder.mkdir()
            inputs = folder / 'input.txt'
            inputs.write_text(moving_script(role == 'host'))
            env = {k: v for k, v in os.environ.items() if not k.startswith('HOTD2_')}
            env.update(HOTD2_PLAY_MENU='1', HOTD2_PLAY_AIMLOG='1', HOTD2_NET_LOG='1')
            if case == 'aim-relay':
                env['HOTD2_NET_NODIRECT'] = '1'
            log = (output / f'{role}.log').open('w')
            logs.append(log)
            args = [str(exe), str(REPO / 'game'), '--scene', str(REPO / 'scenes/st1_play.scene'),
                    '--script', str(inputs), '--pace', '--trace', '--screenshot', str(folder / 'frame-%d.png'),
                    '--every', '200', '--every-from', '2800', '--frame', '2800' if role == early else '3000',
                    '--port', '0', '--lobby', f'127.0.0.1:{server.sock.getsockname()[1]}',
                    '--width', '640', '--height', '480']
            procs[role] = subprocess.Popen(args, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                                          creationflags=subprocess.CREATE_NO_WINDOW)
        deadline = time.monotonic() + 100
        while any(p.poll() is None for p in procs.values()):
            if time.monotonic() > deadline:
                raise TimeoutError(f'{case}: see {output}')
            time.sleep(.2)
        assert all(p.returncode == 0 for p in procs.values()), {r: p.returncode for r, p in procs.items()}
    finally:
        for p in procs.values():
            if p.poll() is None:
                p.kill()
                p.wait()
        for log in logs:
            log.close()
        stop.set()
        worker.join(timeout=2)
        server.sock.close()
        if port_guard:
            port_guard.close()
    results = {}
    for role in ('host', 'join'):
        text = (output / f'{role}.log').read_text()
        local = 1 if role == 'host' else 2
        assert f'connected as P{local} (lobby)' in text, text[-3000:]
        assert 'DESYNC' not in text, text[-3000:]
        frames = {}
        for fr, p, model, x, y, keyboard, device in re.findall(
                r'crosshair: frame (\d+) P(\d) id ([A-F0-9]+) x (\d+) y (\d+) keyboard P(\d) device (-?\d+)', text):
            frames.setdefault(int(fr), {})[int(p)] = dict(x=int(x), y=int(y), keyboard=int(keyboard),
                                                         device=int(device), sprite=model)
        assert frames[2800][1]['x'] == 128 and frames[2800][1]['y'] == 144, frames
        assert frames[2800][2]['x'] == 512 and frames[2800][2]['y'] == 336, frames
        if role != early:
            expected = {1: (416, 168), 2: (160, 288)}
            for player, (x, y) in expected.items():
                actual = frames[3000][player]
                if early and player != local:
                    assert (actual['x'], actual['y']) == (65535, 65535), actual
                else:
                    assert (actual['x'], actual['y']) == (x, y), actual
                assert actual['keyboard'] == local, actual
                assert actual['device'] == (0 if player == local else -1), actual
            if early:
                assert f'continuing alone as P{local} (device 0 -> P{local})' in text, text[-3000:]
            else:
                assert 'continuing alone' not in text, text[-3000:]
                path = 'server relay' if case == 'aim-relay' else 'direct connection'
                assert f'via {path},' in text, text[-3000:]
        results[role] = frames
    report = dict(case=case, passed=True, output=str(output), results=results)
    (output / 'verification.json').write_text(json.dumps(report, indent=2))
    print(f'PASS {case}: both crosshairs checked at frame 2800; movement/remaining player checked at 3000', flush=True)
    return report


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=REPO / 'build/src/port/Release/hotd2.exe')
    ap.add_argument('--cases', nargs='+', choices=('aim-direct', 'aim-relay', 'host-leaves', 'join-leaves'),
                    default=['aim-direct', 'aim-relay', 'host-leaves', 'join-leaves'])
    args = ap.parse_args()
    for case in args.cases:
        run(case, args.exe.resolve())
