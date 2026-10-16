"""Real Randomizer clients: relay, P2 process loss, continued play and replay-based rejoin.

Uses the existing isolated lobby/recovery harness; never touches a user's live game.
"""
import json
import re
from pathlib import Path
import netplay_rejoin_smoke as harness

base_script = harness.script
def randomizer_script(mode, host):
    # The connection resets the world timeline to 901. Point at Randomizer in the shared
    # room before the first repeated confirm; the outer headless lobby uses button navigation.
    rows = [line.split() for line in base_script(mode, host).splitlines()]
    for row in rows:
        row[3] = '.57' if int(row[0]) < 1400 else '.45'
    # Clear the protected opening encounter before exercising randomized spawns.
    # Keep reload/back after the public-room handshake; it would cancel a waiting room.
    rows = [row for row in rows if int(row[0]) < 1500]
    for frame in range(1500, 3990, 12):
        # The fixed aim intentionally lets the larger encounter defeat P1. Continue before
        # the game-over countdown ends, so the recovery assertion remains in this run.
        buttons = 4 if frame == 3300 else 2 if (frame - 1500) % 120 == 0 else 1
        for player in (0, 1):
            rows += [[str(frame), str(player), '.5', '.45', str(buttons)],
                     [str(frame + 2), str(player), '.5', '.45', '0']]
    rows.sort(key=lambda row: (int(row[0]), int(row[1])))
    return ''.join(' '.join(row) + '\n' for row in rows)

if __name__ == '__main__':
    harness.script = randomizer_script
    report = harness.run(harness.REPO / 'build/src/port/Release/play.exe', 'join', True, crash=True)
    output = Path(report['output'])
    cosmetics = []
    for role in ('host', 'join'):
        text = (output / f'{role}.log').read_text()
        if role == 'join': text += (output / 'join-return.log').read_text()
        assert 'network room: host selected 5' in text, text[-4000:]
        assert 'randomizer: encounter' in text, text[-4000:]
        assert 'collision mesh address outside the loaded files' not in text, text[-4000:]
        swaps = set(re.findall(r'randomizer: (?:costume|dialogue) \d+ -> \d+', text))
        assert any('costume' in swap for swap in swaps), 'no scene costume reached'
        assert any('dialogue' in swap for swap in swaps), 'no dialogue replacement reached'
        cosmetics.append(swaps)
    assert cosmetics[0] == cosmetics[1], cosmetics
    report['randomizer'] = True
    report['matching_cosmetic_swaps'] = sorted(cosmetics[0])
    (output / 'verification.json').write_text(json.dumps(report, indent=2))
    print('PASS Randomizer selected, randomized encounters reached, P2 rejoined with matching world hash')
