"""Actual clients: host's recorded chapter clear and guest confirms both results pages.

Uses isolated folders and a direct loopback connection. Requires build/script_auto_st1.txt.
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time

REPO = Path(__file__).resolve().parents[1]


def run(exe=None):
    output = Path(tempfile.mkdtemp(prefix='hotd2_results_net_'))
    print(output, flush=True)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    config = output / 'start.txt'
    config.write_text('gameMode 6\neventMode 0\nscene 0\nstartFrame 0\nseedCounter 902\n'
                      'playerState 0 9\nfreePlay 0\ncredits 6\ncreditSetting 5\n')
    events = [(0, 0, '.5', '.5', 1)]
    for line in (REPO / 'build/script_auto_st1.txt').read_text().splitlines():
        fields = line.split()
        if len(fields) != 5 or fields[0].startswith('#'):
            continue
        f, p, x, y, buttons = fields
        if int(f) > 902:
            events.append((int(f) - 902, int(p), x, y, int(buttons)))
    # An inactive guest may also confirm results; press again after the results minimum.
    events += [(13100, 1, '.5', '.5', 4), (13102, 1, '.5', '.5', 0),
               (13600, 1, '.5', '.5', 4), (13602, 1, '.5', '.5', 0)]
    inputs = output / 'inputs.txt'
    inputs.write_text(''.join(' '.join(map(str, row)) + '\n' for row in sorted(events)))
    env = {k:v for k,v in os.environ.items() if not k.startswith('HOTD2_')}
    env['HOTD2_STAGE_RESULTS'] = '1'
    processes, logs = [], []
    try:
        for role in ('host', 'join'):
            folder = output / role
            folder.mkdir()
            log = (output / f'{role}.log').open('w'); logs.append(log)
            args = [str(exe or REPO / 'build/src/port/Release/play.exe'), str(REPO / 'game'),
                    '--config', str(config), '--seed', '902', '--credits', '6', '--net-delay', '1',
                    '--scene', str(REPO / 'scenes/st1_play.scene'), '--script', str(inputs), '--trace',
                    '--screenshot', str(folder / 'frame-%d.png'), '--frame', '14700',
                    '--every', '100', '--every-from', '13400', '--width', '640', '--height', '480']
            args += ['--host', str(port)] if role == 'host' else ['--join', f'127.0.0.1:{port}']
            processes.append(subprocess.Popen(args, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                                              creationflags=subprocess.CREATE_NO_WINDOW))
        deadline = time.monotonic() + 60
        while any(p.poll() is None for p in processes):
            if time.monotonic() > deadline:
                raise TimeoutError(str(output))
            time.sleep(.1)
        assert all(p.returncode == 0 for p in processes)
        signatures = []
        for role in ('host', 'join'):
            text = (output / f'{role}.log').read_text()
            assert 'DESYNC' not in text, text[-2000:]
            opened = re.search(r'trace frame (\d+) note: stage results: chapter 1', text)
            continued = re.search(r'trace frame (\d+) note: stage results: continue', text)
            assert opened and continued, text[-2500:]
            assert int(continued[1]) - int(opened[1]) >= 180
            final = re.search(r'final state hash ([A-F0-9]+) at frame 14700', text)
            assert final
            signatures.append((opened[1], continued[1], final[1]))
        assert signatures[0] == signatures[1], signatures
        report = dict(passed=True, output=str(output), opened=signatures[0][0], continued=signatures[0][1], hash=signatures[0][2])
        (output / 'verification.json').write_text(json.dumps(report, indent=2))
        print(json.dumps(report), flush=True)
    finally:
        for p in processes:
            if p.poll() is None:
                p.kill(); p.wait()
        for log in logs:
            log.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, help='Path to the console play.exe build to test')
    run(parser.parse_args().exe)
