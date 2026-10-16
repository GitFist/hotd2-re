"""Real clients: host chooses modes, guest pauses, both return to room; Esc never exits.

Runs isolated clients and a local lobby unless --lobby names the deployed service.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import threading
import time
from PIL import Image
from netplay_relay_smoke import REPO, lobby, script
from netplay_rejoin_smoke import rejoin_script


def verify_output(output, rejoin=False):
    hashes = []
    for role in ('host', 'join'):
        log = (output / f'{role}.log').read_text()
        if rejoin and role == 'join':
            returned = (output / 'join-return.log').read_text()
            assert 'rejoined as P2' in returned, returned[-4000:]
            log += returned
        assert 'DESYNC' not in log and 'rejoin failed' not in log, log[-4000:]
        frames = {int(f): (int(mode), int(paused)) for f, mode, paused in re.findall(
            r'checkpoint frame (\d+) mode (\d+) paused (\d+)', log)}
        # Recovery may replay across render checkpoints; validate the returning peer before/after catch-up.
        checkpoints = (1050, 2600, 4650) if rejoin and role == 'join' else (1050, 2600, 3350, 4000, 4650)
        for frame in checkpoints:
            assert frames[frame] == (15, 0), (role, frame, frames[frame], output)
        assert frames[2300] == frames[2400] == (6, 0), (role, frames)
        assert frames[2450] == (6, 1), (role, frames[2450])
        for choice in range(4):
            assert f'network room: host selected {choice}' in log, (role, choice, output)
        assert frames[4950] == (6, 0)
        # The menu must actually render after leaving the stage, not just run invisibly.
        a = Image.open(output / role / 'frame-2600.png').convert('RGB')
        b = Image.open(output / role / 'frame-4650.png').convert('RGB')
        assert a.tobytes() == b.tobytes(), 'room appearance changed between games'
        bright = sum(1 for r, g, b in a.crop((180, 120, 470, 265)).getdata() if min(r, g, b) > 150)
        assert bright > 800, ('missing mode labels', bright, role, output)
        hashes.append(re.search(r'final state hash ([A-F0-9]+) at frame 4950', log)[1])
    assert hashes[0] == hashes[1], hashes
    return hashes[0]


def run(exe, relay=False, endpoint=None, rejoin=False):
    output = Path(tempfile.mkdtemp(prefix='hotd2_room_'))
    print(f'Room lifecycle test: {output}', flush=True)
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
                pass
    import socket
    worker = threading.Thread(target=service, daemon=True)
    worker.start()
    processes, logs = [], []
    host = [(1100, 8), (2250, 4096), (2350, 8)]
    host += [(f, 32) for f in (2460, 2470, 2480, 2490, 2500)]
    host += [(2520, 8), (2540, 128), (2560, 8), (2620, 32), (2640, 8),
             (3300, 4096), (3420, 32), (3440, 32), (3470, 8), (3980, 4096),
             (4040, 32), (4060, 32), (4080, 32), (4100, 8), (4600, 4096), (4700, 8)]
    guest = [(1000, 32), (1020, 8), (2280, 4096), (2400, 4096)]
    try:
        for role, events in (('host', host), ('join', guest)):
            folder = output / role
            folder.mkdir()
            inputs = folder / 'input.txt'
            inputs.write_text(script('public', role == 'host'))
            keys = folder / 'keys.txt'
            keys.write_text(''.join(f'{frame} {bits}\n{frame + 2} 0\n' for frame, bits in events))
            env = {k: v for k, v in os.environ.items() if not k.startswith('HOTD2_')}
            env.update(HOTD2_PLAY_MENU='1', HOTD2_PLAY_KEYS=str(keys), HOTD2_NET_LOG='1')
            if relay:
                env['HOTD2_NET_NODIRECT'] = '1'
            log = (output / f'{role}.log').open('w')
            logs.append(log)
            args = [str(exe), str(REPO / 'game'), '--scene', str(REPO / 'scenes/st1_play.scene'),
                    '--script', str(inputs), '--pace', '--trace', '--screenshot', str(folder / 'frame-%d.png'),
                    '--every', '50', '--every-from', '1050', '--frame', '4950', '--port', '0',
                    '--lobby', endpoint or f'127.0.0.1:{server.sock.getsockname()[1]}', '--width', '640', '--height', '480']
            processes.append(subprocess.Popen(args, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                                              creationflags=subprocess.CREATE_NO_WINDOW))
        if rejoin:
            deadline = time.monotonic() + 85
            while 'checkpoint frame 3350 mode 15' not in (output / 'join.log').read_text():
                if time.monotonic() > deadline:
                    raise TimeoutError('guest did not reach second lobby')
                time.sleep(.1)
            processes[1].kill(); processes[1].wait()
            while not (output / 'host/frame-3600.png').exists():
                if time.monotonic() > deadline:
                    raise TimeoutError('host did not keep playing')
                time.sleep(.1)
            inputs.write_text(rejoin_script())  # last launch's inputs/env/args belong to the guest
            log = (output / 'join-return.log').open('w'); logs.append(log)
            processes.append(subprocess.Popen(args, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                                              creationflags=subprocess.CREATE_NO_WINDOW))
        deadline = time.monotonic() + 130
        while any(p.poll() is None for p in processes):
            if time.monotonic() > deadline:
                raise TimeoutError(f'clients did not complete: {output}')
            time.sleep(.2)
        assert all(p.returncode == 0 for i, p in enumerate(processes) if not (rejoin and i == 1))
        final_hash = verify_output(output, rejoin)
        report = dict(passed=True, relay=relay, lobby=endpoint, rejoin=rejoin, output=str(output), hash=final_hash)
        (output / 'verification.json').write_text(json.dumps(report, indent=2))
        print(f'PASS all modes, host authority, Esc pause/resume, Enter confirm, shared Return to Lobby: {output}', flush=True)
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
    ap.add_argument('--relay', action='store_true')
    ap.add_argument('--lobby')
    ap.add_argument('--rejoin', action='store_true', help='crash P2 after returning to the room, then rejoin during a later mode')
    args = ap.parse_args()
    run(args.exe.resolve(), args.relay, args.lobby, args.rejoin)
