"""Build the lossless native audio bank from verified original-driver captures."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import wave
import zlib

import numpy as np
from scipy.signal import correlate
from naomi_audio_source import load_rom, music_plan, effect_plan, RATE, SAMPLES_PER_TICK


def prepare_recording(entry, folder):
    captured = json.loads((folder / (entry['name'] + '.json')).read_text())
    for key, value in entry.items():
        if captured.get(key) != value:
            raise ValueError(f"{entry['name']}: capture metadata changed: {key}")
    with wave.open(str(folder / (entry['name'] + '.wav')), 'rb') as f:
        if (f.getnchannels(), f.getsampwidth(), f.getframerate()) != (2, 2, RATE):
            raise ValueError('Expected stereo PCM16 at the original AICA rate')
        pcm = f.readframes(f.getnframes())
    if hashlib.sha256(pcm).hexdigest() != captured['pcm_sha256']:
        raise ValueError('Capture hash mismatch')
    audio = np.frombuffer(pcm, '<i2').reshape(-1, 2)
    start = end = 0
    measurements = []
    if entry['loop']:
        intro = entry.get('intro_frames', entry.get('intro_ticks', 0) * SAMPLES_PER_TICK)
        period = entry.get('loop_frames', entry.get('loop_ticks', 0) * SAMPLES_PER_TICK)
        # Keep the intro and a steady-state cycle. Choose a phase-equivalent
        # splice into the second pass so intro voices do not recur. Both the
        # intro splice and the repeat seam must match; never retime the period.
        low = intro + period
        count = min(2 * RATE, len(audio) - low - period - 32)
        if count < 64:
            raise ValueError(f"{entry['name']}: capture is too short for a steady-state loop")
        difference = audio[low:low+count+32].astype(float) - audio[low+period:low+period+count+32]
        cost = np.sum(difference * difference, axis=1)
        intro_difference = audio[low-period:low-period+count+32].astype(float) - audio[low:low+count+32]
        cost += np.sum(intro_difference * intro_difference, axis=1)
        window = np.convolve(cost, np.ones(32), 'valid')[:count]
        phase = int(np.argmin(window)) + 16
        start = low + phase
        end = start + period
        # Independently compare three source passages against the next iteration.
        mono = audio.mean(axis=1)
        span = min(RATE, period // 4)
        for fraction in (.05, .4, .75):
            pos = intro + int(period * fraction)
            x = mono[pos:pos+span]
            y = mono[pos+period-8:pos+period+span+8]
            c = correlate(y, x, mode='valid', method='fft')
            lag = int(np.argmax(c)) - 8
            # Quiet/constant passages have several equally good alignments.
            # Prefer the exact authored period when it already matches.
            if np.corrcoef(x, y[8:8+span])[0, 1] > .999999:
                lag = 0
            corr = float(np.corrcoef(x, y[lag+8:lag+8+span])[0, 1])
            if not np.isfinite(corr) or abs(lag) > 3 or corr < .65:
                raise ValueError(f"{entry['name']}: loop clock validation failed: lag={lag}, correlation={corr}")
            measurements.append(dict(lag=lag, correlation=round(corr, 6)))
        seam_error = float(np.max(np.abs(audio[start].astype(float) - audio[end]))) / 32768
        splice_error = float(np.max(np.abs(audio[start-period].astype(float) - audio[start]))) / 32768
        if max(seam_error, splice_error) > .03:
            raise ValueError(f"{entry['name']}: discontinuous loop seam ({seam_error})")
        audio = np.concatenate((audio[:start-period], audio[start:end]))
        start -= period
        end -= period
    else:
        # Preserve genuine rests; trim only the tail after the final nonzero sample.
        nonzero = np.flatnonzero(np.any(audio != 0, axis=1))
        if not len(nonzero):
            raise ValueError('Empty one-shot capture')
        audio = audio[:min(len(audio), int(nonzero[-1]) + 1 + RATE // 20)]
        seam_error = 0
        splice_error = 0
    buffer = io.BytesIO()
    with wave.open(buffer, 'wb') as f:
        f.setparams((2, 2, RATE, 0, 'NONE', 'not compressed'))
        f.writeframes(audio.tobytes())
    wav = buffer.getvalue()
    return wav, dict(index=entry['index'], name=entry['name'], start=start, end=end,
                     frames=len(audio), seam_error=seam_error, splice_error=splice_error, loop_checks=measurements,
                     source_sha256=entry['source_sha256'], wav_sha256=hashlib.sha256(wav).hexdigest())


def build_bank(rom, captures, output):
    _, banks = load_rom(rom)
    plan = music_plan(banks) + effect_plan(banks)
    rows, payload, report = [], [], []
    offset = 16 + 24 * len(plan)
    for entry in plan:
        wav, info = prepare_recording(entry, captures)
        compressed = zlib.compress(wav, 9)
        rows.append(struct.pack('<6I', entry['index'], offset, len(compressed), len(wav), info['start'], info['end']))
        offset += len(compressed)
        payload.append(compressed)
        report.append(info)
        print(entry['name'], f"{len(wav)/1048576:.2f} MiB", f"loop [{info['start']},{info['end']})", flush=True)
    data = struct.pack('<8sII', b'H2NAUDIO', 1, len(plan)) + b''.join(rows) + b''.join(payload)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    output.with_suffix('.json').write_text(json.dumps(dict(format=1, bytes=len(data), sha256=digest, recordings=report), indent=2) + '\n')
    print(f'{len(data)} bytes; SHA-256 {digest}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('rom', 'captures', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    build_bank(args.rom, args.captures, args.output)
