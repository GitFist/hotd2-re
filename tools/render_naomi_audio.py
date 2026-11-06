"""Offline original-driver renderer. Requires the verified ROM, BIOS and Flycast 2.7.

The isolated emulator's SDL underrun fill is tagged, not its AICA synthesis. This
lets the disk capture discard host underruns while retaining real musical rests.
No emulator or ROM is included in the resulting audio bank or native game.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import time
import wave

import numpy as np
from naomi_audio_source import load_rom, music_plan, effect_plan, RATE

OFFICIAL_SHA256 = 'f41c1a4243274caf9a183114bd610407835521a011a48b18326e5e957e6f4e6b'


def prepare(exe, reference, state, output):
    output.mkdir(parents=True, exist_ok=True)
    binary = bytearray(exe.read_bytes())
    if hashlib.sha256(binary).hexdigest() != OFFICIAL_SHA256:
        raise ValueError('Expected official Flycast 2.7 Windows executable')
    offset = 0xa23caa
    if binary[offset:offset + 12] != bytes.fromhex('31d24889e9e854f4f6ffeb95'):
        raise ValueError('Unexpected SDL underrun memset instruction')
    binary[offset:offset + 2] = b'\xb2\x7f'
    (output / 'flycast-capture.exe').write_bytes(binary)
    (output / 'data').mkdir(exist_ok=True)
    for name in ('hod2bios.zip', 'hotd2.zip.eeprom', 'hotd2.zip.nvmem'):
        shutil.copyfile(reference / 'data' / name, output / 'data' / name)
    shutil.copyfile(state, output / 'data' / 'hotd2.state')
    config = (reference / 'emu.cfg').read_text()
    for key in ('DiscordPresence', 'FetchBoxart', 'UploadCrashLogs', 'rend.FramePacing', 'rend.ThreadedRendering'):
        config = config.replace(key + ' = yes', key + ' = no')
    (output / 'emu.cfg').write_text(config)


def capture(rom, output, entry):
    name = entry['name']
    end_frame = 150 + math.ceil(entry['duration'] * 60)
    lua = '''local n=0
local f=io.open("events.log","w")
flycast_callbacks={vblank=function()
 n=n+1
 if n>=30 and n<50 then flycast.input.pressButtons(1,8192) end
 if n==50 then flycast.input.releaseButtons(1,8192) end
 if n==100 then flycast.memory.write32(0x0c403468,0x0c0a7ef0) end
 if n==150 then
  flycast.memory.write32(0x0c9bf750,SELECTOR)
  flycast.memory.write32(0x0c113b14+SELECTOR*4,COMMAND)
  flycast.memory.write16(0x0c0a7fa8,0x0009)
 end
 if n==151 then flycast.memory.write16(0x0c0a7fa8,0x896e) end
 if n==210 then f:write(string.format("command %08x\\n",flycast.memory.read32(0x00800048)));f:flush() end
 if n==END_FRAME then f:write("DONE\\n");f:flush();flycast.emulator.exit() end
end}
'''.replace('SELECTOR', str(entry['selector'])).replace('COMMAND', str(entry['command'])).replace('END_FRAME', str(end_frame))
    (output / 'flycast.lua').write_text(lua)
    events = output / 'events.log'
    events.unlink(missing_ok=True)
    env = os.environ.copy()
    env.update(SDL_AUDIODRIVER='disk', SDL_DISKAUDIOFILE='capture.raw', SDL_DISKAUDIODELAY='2')
    # This SDL build reuses its Windows getenv buffer: the delay becomes the
    # filename. Its callback is 1024 stereo int16 frames, including underruns.
    raw = output / '2'
    raw.unlink(missing_ok=True)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    started = time.monotonic()
    with (output / 'capture.log').open('w') as log:
        proc = subprocess.Popen([str(output / 'flycast-capture.exe'), str(rom)], cwd=output,
                                env=env, stdout=log, stderr=log, startupinfo=startup)
        try:
            while proc.poll() is None and time.monotonic() - started < max(180, entry['duration'] * 2):
                time.sleep(.2)
                if events.exists() and 'DONE' in events.read_text():
                    break
            if not events.exists() or 'DONE' not in events.read_text():
                raise RuntimeError(f'{name}: renderer did not finish')
        finally:
            if proc.poll() is None:
                proc.terminate()
            proc.wait(timeout=10)
    event_text = events.read_text()
    if f"command {entry['command']:08x}" not in event_text:
        raise ValueError(f'{name}: original driver did not acknowledge the expected song: {event_text}')
    blocks = np.fromfile(raw, dtype=np.uint8).reshape(-1, 4096)
    underruns = np.all(blocks == 0x7f, axis=1)
    pcm = blocks[~underruns].copy().view('<i2').reshape(-1, 2)
    expected = end_frame / 60 * RATE
    if abs(len(pcm) - expected) > RATE * 2 + expected * .005:
        raise ValueError(f'{name}: capture length {len(pcm) / RATE:.3f}s disagrees with emulation')
    nonzero = np.flatnonzero(np.any(pcm[2 * RATE:] != 0, axis=1))
    if not len(nonzero):
        raise ValueError(f'{name}: empty music capture')
    onset = 2 * RATE + int(nonzero[0])
    if np.any(pcm[onset - RATE // 4:onset] != 0):
        raise ValueError(f'{name}: old audio overlaps song start')
    pcm = pcm[onset:]
    with wave.open(str(output / (name + '.wav')), 'wb') as wav:
        wav.setparams((2, 2, RATE, 0, 'NONE', 'not compressed'))
        wav.writeframes(pcm.tobytes())
    entry.update(capture_frames=len(pcm), onset_frame=onset,
                 discarded_host_underruns=int(underruns.sum()), pcm_sha256=hashlib.sha256(pcm).hexdigest())
    raw.unlink()
    print(f'{name}: {len(pcm) / RATE:.2f}s captured in {time.monotonic() - started:.1f}s; '
          f'{int(underruns.sum())} host underruns excluded', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('rom', 'flycast', 'reference', 'state', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--only', nargs='*')
    args = parser.parse_args()
    program, banks = load_rom(args.rom)
    plan = music_plan(banks) + effect_plan(banks)
    prepare(args.flycast, args.reference, args.state, args.output)
    for entry in plan:
        if args.only and entry['name'] not in args.only:
            continue
        result = args.output / (entry['name'] + '.json')
        if result.exists() and (args.output / (entry['name'] + '.wav')).exists():
            continue
        capture(args.rom.resolve(), args.output.resolve(), entry)
        result.write_text(json.dumps(entry, indent=2) + '\n')


if __name__ == '__main__':
    main()
