"""Verify a complete Windows ZIP, then exercise its clients through the configured live lobby.

Usage: python tools/verify_complete_dist.py build/dist-g/hotd2re-complete.zip
Runs direct, relay-only, and direct-to-relay failover cases on this PC. It does not substitute for a
separate-household playtest. Only test processes started here are terminated on failure.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time
import zipfile

def packaged_script(host, final_frame):
    """Aim at the current clickable menus and use an unlisted test room."""
    def event(frame, x, y, buttons=1):
        return f'{frame} 0 {x/640} {y/480} {buttons}\n{frame+4} 0 {x/640} {y/480} 0\n'
    inputs = event(100, 320, 240) + event(200, 320, 240, 4) + event(300, 320, 240, 4)
    inputs += event(400, 40, 338) + event(450, 320, 202)  # Network > Peer to Peer
    inputs += event(500, 320, 142 if host else 172)
    # Both peers reset to frame 901 when the shared lobby starts. Only P1 picks Arcade.
    if host:
        inputs += event(1000, 240, 142)
    for frame in range(1500, final_frame - 50, 24):
        buttons = 2 if frame % 120 == 20 else 1
        inputs += event(frame, 320, 240, buttons)
    return inputs


def sha256(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def check_files(folder):
    lines = (folder / 'SHA256SUMS.txt').read_text().splitlines()
    for line in lines:
        expected, relative = line.split('  ', 1)
        assert sha256(folder / relative) == expected, relative
    return len(lines)


def run_case(case, copies, scratch):
    final_frame = 5000 if case == 'failover' else 3000
    output = Path(tempfile.mkdtemp(prefix=case + '_', dir=scratch))
    print(f'{case}: testing packaged clients; final frame {final_frame}', flush=True)
    processes, logs = [], []
    # A prior screenshot test room can remain until the server's 30 s expiry. Prevent reuse of the
    # fixed host port across consecutive cases while that session is still registered.
    # The game's normal fallback binds a fresh ephemeral port when 27015 is occupied.
    port_guard = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        port_guard.bind(('0.0.0.0', 27015))
    except OSError:
        port_guard.close()
        port_guard = None  # an existing app already prevents use of this fixed port
    try:
        for role, folder in copies.items():
            inputs = packaged_script(role == 'host', final_frame)
            input_file = output / f'{role}-input.txt'
            input_file.write_text(inputs)
            env = {key: value for key, value in os.environ.items() if not key.startswith('HOTD2_')}
            env['HOTD2_PLAY_MENU'] = '1'
            env['HOTD2_NET_LOG'] = '1'
            if role == 'join':
                deadline = time.monotonic() + 25
                while True:
                    match = re.search(r'netplay: room \d+ code (\d+)', (output / 'host.log').read_text())
                    if match:
                        env['HOTD2_NET_PEERCODE'] = match[1]
                        break
                    if time.monotonic() > deadline:
                        raise TimeoutError(f'Host did not create the unlisted test room; see {output}')
                    time.sleep(0.1)
            if case == 'relay':
                env['HOTD2_NET_NODIRECT'] = '1'
            elif case == 'failover' and role == 'join':
                # Wait past the server's 30 s relay TTL: idle-path keepalives must preserve the fallback.
                env['HOTD2_NET_CUTDIRECT'] = '40'
            log = (output / f'{role}.log').open('w')
            logs.append(log)
            args = [str(folder / 'hotd2.exe'), '--script', str(input_file), '--pace', '--trace',
                    '--screenshot', str(output / f'{role}.png'), '--frame', str(final_frame),
                    '--port', '0', '--width', '640', '--height', '480']
            processes.append(subprocess.Popen(args, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                                              creationflags=subprocess.CREATE_NO_WINDOW))
        deadline = time.monotonic() + 155
        while any(process.poll() is None for process in processes):
            if time.monotonic() > deadline:
                raise TimeoutError(f'{case} did not finish within 155 seconds; see {output}')
            time.sleep(0.2)
        assert all(process.returncode == 0 for process in processes), [p.returncode for p in processes]
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait()
        for log in logs:
            log.close()
        if port_guard is not None:
            port_guard.close()
    results = {}
    for role in copies:
        text = (output / f'{role}.log').read_text()
        assert f'connected as P{1 if role == "host" else 2} (lobby)' in text, text[-4000:]
        assert f'frame {final_frame} mode ' in text, text[-4000:]
        assert 'DESYNC' not in text and 'continuing alone' not in text, text[-4000:]
        expected_path = 'server relay' if case == 'relay' else 'direct'
        connected = next(line for line in text.splitlines() if 'connected as' in line)
        assert f'via {expected_path}' in connected, connected
        delay = int(re.search(r'input delay (\d+) frames', connected)[1])
        assert 2 <= delay <= 10, connected
        switches = [line for line in text.splitlines() if 'switched to:' in line or 'probing both paths' in line]
        if case == 'failover':
            assert any('switched to: server relay' in line for line in switches), text[-4000:]
        results[role] = dict(connected=connected, switches=switches,
                             final=next(line for line in text.splitlines() if f': frame {final_frame} mode ' in line))
        if case != 'failover':  # failover's longer run can already be at game over
            final = results[role]['final']
            assert f'frame {final_frame} mode 6 ' in final, final
            scenery = re.search(r'stage-models (\d+)', final)
            assert scenery and int(scenery[1]) > 0, f'Missing stage geometry: {final}'
            results[role]['stage_models'] = int(scenery[1])
        print(f'{case} {role}: {connected}', flush=True)
    # Local cursor/connection overlays can add different sprite counts on the two PCs.
    simulation = lambda line: line.split(': frame ')[-1].split(' sprites ')[0]
    assert simulation(results['host']['final']) == simulation(results['join']['final']), results
    report = dict(case=case, passed=True, final_frame=final_frame, results=results, logs=str(output))
    (output / 'results.json').write_text(json.dumps(report, indent=2))
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('archive', type=Path)
    ap.add_argument('--cases', nargs='+', choices=('direct', 'relay', 'failover'), default=['direct', 'relay', 'failover'])
    ap.add_argument('--reuse', type=Path, help='Reuse and reverify extracted copies from a previous run')
    args = ap.parse_args()
    archive = args.archive.resolve()
    package_hash = sha256(archive)
    assert package_hash == Path(str(archive) + '.sha256').read_text().split()[0]
    scratch = args.reuse.resolve() if args.reuse else Path(tempfile.mkdtemp(prefix='hotd2_complete_check_'))
    print(f'Checking/extracting {archive.name}; results in {scratch}', flush=True)
    with zipfile.ZipFile(archive) as package:
        assert package.testzip() is None
        if not args.reuse:
            package.extractall(scratch / 'host')
    copies = dict(host=scratch / 'host/hotd2re', join=scratch / 'join/hotd2re')
    verified_files = check_files(copies['host'])
    assert 'lobby=64.23.186.88:27020' in (copies['host'] / 'hotd2re_settings.ini').read_text()
    assert not list(copies['host'].rglob('PUT_YOUR_GAME_FILES_HERE.txt'))
    assert not (copies['host'] / 'hotd2re_save.bin').exists()
    if not args.reuse:
        shutil.copytree(copies['host'], copies['join'])
    else:
        check_files(copies['join'])
    print(f'ZIP CRCs and {verified_files} file hashes passed.', flush=True)
    report = dict(archive=str(archive), zip_bytes=archive.stat().st_size, sha256=package_hash,
                  verified_files=verified_files, lobby='64.23.186.88:27020',
                  build=(copies['host'] / 'BUILD.txt').read_text(), verification_folder=str(scratch),
                  clients_on_separate_networks_tested=False, cases=[])
    report_path = archive.parent / 'verification.json'
    if args.reuse and report_path.exists():
        previous = json.loads(report_path.read_text())
        assert previous['sha256'] == package_hash and previous['verification_folder'] == str(scratch)
        report['cases'] = [case for case in previous['cases'] if case['case'] not in args.cases]
    for case in args.cases:
        report['cases'].append(run_case(case, copies, scratch))
        (archive.parent / 'verification.json').write_text(json.dumps(report, indent=2))
    for folder in copies.values():
        check_files(folder)
    report['passed'] = True
    (archive.parent / 'verification.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
