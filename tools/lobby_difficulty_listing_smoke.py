"""Capture real Modern Home/Browse and Classic Browse against isolated local room fixtures.

All five advertised difficulties, unknown metadata, private/public rooms, finite/unlimited
continues, and a maximum-width incompatible room are shown without joining any room.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time

from PIL import Image
from netplay_relay_smoke import REPO, lobby


def inputs(modern):
    events = [(700, .5, .25, 4), (704, .5, .25, 0)]
    if modern:
        events += [(890, 450 / 640, 338 / 480, 0),
                   (900, 450 / 640, 338 / 480, 1), (904, 450 / 640, 338 / 480, 0)]
    else:
        for frame in range(740, 851, 20):
            events += [(frame, .5, .25, 16), (frame + 4, .5, .25, 0)]
        events += [(900, .5, .25, 4), (904, .5, .25, 0),
                   (940, .5, .25, 16), (944, .5, .25, 0),
                   (970, .5, .25, 4), (974, .5, .25, 0)]
    return ''.join(f'{frame} 0 {x} {y} {buttons}\n' for frame, x, y, buttons in events)


def run(exe, game, build, timeout=45):
    assert exe.is_file() and (game / 'Hod2.exe').is_file(), (exe, game)
    output = Path(tempfile.mkdtemp(prefix='hotd2_lobby_difficulties_'))
    print(f'Lobby difficulty captures: {output}', flush=True)
    server = lobby.Server('127.0.0.1', 0)
    fixtures = [(1, 0, lobby.F_PRIVATE, build), (2, 4, lobby.F_UNLIMITED, build),
                (3, 2, 0, build), (4, 1, lobby.F_PRIVATE | lobby.F_UNLIMITED, build),
                (5, 3, 0, build), (6, -1, 0, build), (0xFFFFFFFF, 4, lobby.F_PRIVATE, build - 1)]
    for index, (room_id, difficulty, policy, version) in enumerate(fixtures):
        flags = policy | lobby.F_RELAY | lobby.F_REJOIN | ((difficulty + 1) << 5)
        endpoint = ('127.0.0.1', 31000 + index)
        server.rooms[room_id] = lobby.Room(room_id, 12000 + index, version, flags, endpoint, endpoint)
    stop = threading.Event()
    requests = []

    def service():
        while not stop.is_set():
            # These fixtures represent live waiting hosts; no gameplay endpoint is contacted.
            for room in server.rooms.values():
                room.last_refresh = time.monotonic()
            try:
                data, address = server.sock.recvfrom(65536)
                if data[:5] == b'H2LB\1' and len(data) >= 6:
                    requests.append(data[5])
                server.handle(data, address)
            except socket.timeout:
                pass
            except ConnectionResetError:
                pass

    worker = threading.Thread(target=service, daemon=True)
    worker.start()
    processes, logs = [], []
    try:
        for theme in ('nightshift', 'classic'):
            folder = output / theme
            folder.mkdir()
            script = folder / 'inputs.txt'
            script.write_text(inputs(theme == 'nightshift'))
            settings = folder / 'settings.ini'
            settings.write_text(f'menu={theme}\n')
            keys = folder / 'keys.txt'
            keys.write_text('0 0\n')  # Enable diagnostic checkpoints without changing input.
            env = {key: value for key, value in os.environ.items() if not key.startswith('HOTD2_')}
            env.update(HOTD2_PLAY_MENU='1', HOTD2_PLAY_KEYS=str(keys), HOTD2_TEST_HOME_DIRECTORY='1')
            log = (output / f'{theme}.log').open('w')
            logs.append(log)
            args = [str(exe), str(game), '--scene', str(REPO / 'scenes/st1_play.scene'),
                    '--settings', str(settings), '--script', str(script), '--pace', '--trace',
                    '--screenshot', str(folder / 'frame-%d.png'), '--every', '50', '--every-from', '850',
                    '--frame', '1100', '--port', '0', '--lobby', f'127.0.0.1:{server.sock.getsockname()[1]}',
                    '--width', '640', '--height', '480']
            processes.append(subprocess.Popen(args, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)))
        deadline = time.monotonic() + timeout
        while any(process.poll() is None for process in processes):
            if time.monotonic() > deadline:
                raise TimeoutError(f'capture clients did not finish: {output}')
            time.sleep(.2)
        assert all(process.returncode == 0 for process in processes), (output, [p.returncode for p in processes])
        for theme in ('nightshift', 'classic'):
            text = (output / f'{theme}.log').read_text()
            assert 'checkpoint frame 1100 mode 15' in text, (theme, text[-4000:])
            assert 'connected as' not in text and 'DESYNC' not in text, (theme, text[-4000:])
            picture = Image.open(output / theme / 'frame-1100.png')
            assert picture.size == (640, 480)
        assert 'checkpoint frame 850 mode 4' in (output / 'nightshift.log').read_text()
        assert requests.count(3) >= 3, ('Home and both Browse themes must request the actual directory', requests)
        assert 4 not in requests and all(room.joiner_public is None for room in server.rooms.values()), \
            'viewing difficulty labels must not join a room'
        report = dict(passed=True, output=str(output), build=build, fixture_rooms=fixtures,
                      home=str(output / 'nightshift/frame-850.png'),
                      modern_browse=str(output / 'nightshift/frame-1100.png'),
                      classic_browse=str(output / 'classic/frame-1100.png'), directory_requests=requests.count(3))
        (output / 'verification.json').write_text(json.dumps(report, indent=2))
        print(f'PASS captured Home and both Browse themes without joining: {output}', flush=True)
        return report
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


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=REPO / 'build-x64/src/port/Release/play.exe')
    parser.add_argument('--game', type=Path, default=REPO / 'game')
    parser.add_argument('--build', type=int, default=2026102200)
    parser.add_argument('--timeout', type=float, default=45)
    arguments = parser.parse_args()
    run(arguments.exe.resolve(), arguments.game.resolve(), arguments.build, arguments.timeout)
