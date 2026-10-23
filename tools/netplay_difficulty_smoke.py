"""Two real clients: saved host difficulty, host-only lobby edits, and matching gameplay.

Uses isolated native saves, hidden headless clients, and an ephemeral loopback lobby.
The game assets and the player's actual settings/save are never modified.
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

from PIL import Image
from netplay_relay_smoke import REPO, lobby
from tex_export import Exe


def native_save(exe, difficulty):
    """SaveData::Defaults/DefaultSettings, then an independent difficulty preference."""
    base, size = 0x009C9120, 0xF4C
    data = bytearray(size)

    def put(address, value):
        data[address - base:address - base + len(value)] = value

    for table in range(12):
        put(0x009C97A0 + table * 0xA0, exe.bytes(0x00530048 + table * 0xA0, 0xA0))
    for address, value in ((0x009C9F22, 1), (0x009C9F23, 1), (0x009C9F27, 1),
                           (0x009C9F40, 1), (0x009C9F44, 1), (0x009C9F4D, 1),
                           (0x009CA05A, 0x90), (0x009CA05B, 0xC0),
                           (0x009CA058, 7), (0x009CA05F, 7)):
        put(address, bytes([value]))
    for address, source in ((0x009C9F20, 0x004C42A0), (0x009C9F25, 0x004C42A2),
                            (0x009C9F28, 0x004C4368), (0x009C9F21, 0x004C42A1)):
        put(address, exe.bytes(source, 1))
    for player in range(2):
        start = 0x009C9F64 + player * 0x7C
        put(start - 4, exe.bytes(0x004C42A3, 1))
        put(start, exe.bytes(0x004C42A4, 4))
        put(start + 4, exe.bytes(0x004C42A8 + player * 0x50, 0x50))
        put(start + 0x54, exe.bytes(0x004C4348, 4))
        put(start + 0x58, exe.bytes(0x004C434C, 4))
        put(start + 0x5C, exe.bytes(0x004C4350 + player * 0x50, 0x14))
    put(0x009C9F20, bytes([difficulty]))
    return b'HOTD2RE1' + data + struct.pack('<I', -sum(data) & 0xFFFFFFFF)


def pointer_script(host):
    # Actual Night Shift Home -> Host/Browse actions, then the connection screen.
    x, y = (450 / 640, 298 / 480) if host else (450 / 640, 338 / 480)
    events = [(700, .5, .25, 4), (712, .5, .25, 0),
              (800, x, y, 0), (900, x, y, 1), (904, x, y, 0)]
    if host:
        events += [(1000, .5, 200 / 480, 0), (1080, .5, 200 / 480, 1), (1084, .5, 200 / 480, 0)]
    else:
        events += [(1100, .5, 104 / 480, 0), (1200, .5, 104 / 480, 1), (1204, .5, 104 / 480, 0)]
    return ''.join(f'{frame} {player} {x} {y} {buttons}\n'
                   for frame, x, y, buttons in events for player in (0, 1))


def keyboard_script(host):
    if host:
        events = [(1250 + i * 10, 32) for i in range(6)]  # Select Difficulty.
        events += [(1450, 128), (1470, 128)]  # Easy -> Normal -> Hard.
        events += [(1530 + i * 10, 16) for i in range(6)]  # Return to Arcade.
        events += [(1620, 8)]
    else:
        # Guest attempts changes while the host has selected the difficulty row.
        events = [(1320, 128), (1340, 8), (1360, 64)]
    return ''.join(f'{frame} {bits}\n{frame + 2} 0\n' for frame, bits in events)


def verify(output):
    hashes, crops, checkpoints = [], {}, {}
    for role in ('host', 'join'):
        text = (output / f'{role}.log').read_text()
        assert f'connected as P{1 if role == "host" else 2} (lobby)' in text, text[-5000:]
        assert not any(word in text for word in ('DESYNC', 'continuing alone', 'rejoin failed')), text[-5000:]
        frames = {int(f): tuple(map(int, values)) for f, *values in re.findall(
            r'checkpoint frame (\d+) mode (\d+) paused (\d+) type (\d+) rank (-?\d+) difficulty (\d+)', text)}
        for frame, difficulty in ((1100, 1), (1400, 1), (1500, 3)):
            assert frames[frame][0:2] == (15, 0) and frames[frame][4] == difficulty, (role, frame, frames)
        assert frames[1700] == (6, 0, 0, 12, 3), (role, frames[1700])
        assert frames[2300][0] == 6 and frames[2300][4] == 3, (role, frames[2300])
        assert 'network room: host selected 0' in text, text[-5000:]
        # Both snapshots select the same row; changed pixels must include the new difficulty text.
        easy = Image.open(output / role / 'frame-1400.png').convert('RGB').crop((180, 285, 590, 316))
        hard = Image.open(output / role / 'frame-1500.png').convert('RGB').crop((180, 285, 590, 316))
        assert easy.tobytes() != hard.tobytes(), (role, 'difficulty label did not change')
        pixels = hard.tobytes()
        red = sum(pixels[i] > 140 and max(pixels[i + 1:i + 3]) < 100 for i in range(0, len(pixels), 3))
        assert red > 100, (role, 'selected difficulty text is missing')
        crops[role] = hard.tobytes()
        gameplay = re.search(r'frame 2300 mode 6 .*stage-models (\d+)', text)
        assert gameplay and int(gameplay[1]) > 0, (role, 'playable stage did not load', text[-3000:])
        match = re.search(r'final state hash ([A-F0-9]+) at frame 2300', text)
        assert match, text[-5000:]
        hashes.append(match[1])
        checkpoints[role] = frames
        # Room changes never overwrite either player's normal local difficulty.
        save = (output / role / 'hotd2re_save.bin').read_bytes()
        assert save[8 + 0x009C9F20 - 0x009C9120] == (1 if role == 'host' else 4)
    assert hashes[0] == hashes[1], hashes
    assert crops['host'] == crops['join'], 'clients display different lobby difficulty labels'
    return dict(passed=True, output=str(output), hash=hashes[0], checkpoints=checkpoints,
                initial_host_difficulty='Easy', initial_guest_difficulty='Very Hard',
                room_difficulty='Hard', initial_gameplay_rank=12, players=2)


def run(exe, game, timeout=65):
    assert exe.is_file() and (game / 'Hod2.exe').is_file(), (exe, game)
    output = Path(tempfile.mkdtemp(prefix='hotd2_difficulty_'))
    print(f'Difficulty smoke test: {output}', flush=True)
    server = lobby.Server('127.0.0.1', 0)
    stop = threading.Event()

    def service():
        while not stop.is_set():
            try:
                data, address = server.sock.recvfrom(65536)
                server.handle(data, address)
            except socket.timeout:
                server.expire()
            except ConnectionResetError:
                pass

    worker = threading.Thread(target=service, daemon=True)
    worker.start()
    processes, logs = [], []
    image = Exe(str(game / 'Hod2.exe'))
    try:
        for role in ('host', 'join'):
            folder = output / role
            folder.mkdir()
            save = folder / 'hotd2re_save.bin'
            save.write_bytes(native_save(image, 1 if role == 'host' else 4))
            inputs, keys = folder / 'input.txt', folder / 'keys.txt'
            inputs.write_text(pointer_script(role == 'host'))
            keys.write_text(keyboard_script(role == 'host'))
            env = {key: value for key, value in os.environ.items() if not key.startswith('HOTD2_')}
            env.update(HOTD2_PLAY_MENU='1', HOTD2_PLAY_KEYS=str(keys), HOTD2_NET_LOG='1')
            log = (output / f'{role}.log').open('w')
            logs.append(log)
            args = [str(exe), str(game), '--scene', str(REPO / 'scenes/st1_play.scene'),
                    '--save', str(save), '--script', str(inputs), '--pace', '--trace',
                    '--screenshot', str(folder / 'frame-%d.png'), '--every', '100', '--every-from', '1100',
                    '--frame', '2300', '--port', '0', '--lobby', f'127.0.0.1:{server.sock.getsockname()[1]}',
                    '--width', '640', '--height', '480']
            processes.append(subprocess.Popen(args, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)))
        deadline = time.monotonic() + timeout
        while any(process.poll() is None for process in processes):
            if time.monotonic() > deadline:
                raise TimeoutError(f'clients did not finish: {output}')
            time.sleep(.2)
        assert all(process.returncode == 0 for process in processes), (output, [p.returncode for p in processes])
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait()
        for log in logs:
            log.close()
        stop.set()
        worker.join(timeout=2)
        server.sock.close()
    report = verify(output)
    (output / 'verification.json').write_text(json.dumps(report, indent=2))
    print(f'PASS saved host Easy, guest authority guard, shared Hard, gameplay rank 12, matching {report["hash"]}: {output}',
          flush=True)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=REPO / 'build-x64/src/port/Release/play.exe')
    parser.add_argument('--game', type=Path, default=REPO / 'game')
    parser.add_argument('--timeout', type=float, default=65)
    arguments = parser.parse_args()
    run(arguments.exe.resolve(), arguments.game.resolve(), arguments.timeout)
