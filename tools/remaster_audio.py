"""WAV resampling for tools/remaster.py: polyphase windowed-sinc (Kaiser) to a new rate, 16-bit PCM out.

No filtering beyond the anti-imaging / anti-aliasing low-pass that the resampler itself is, no gain change, no
dither (plain rounding). Samples that round past 16-bit full scale are clipped and counted (ringing of a
full-scale source can overshoot by a few LSB). Up-sampling adds no content above the source's Nyquist frequency:
a 22.05 kHz file at 48 kHz still has nothing above 11 kHz; what the pack buys is an offline high-order conversion
(and a uniform rate) instead of whatever the audio device's real-time resampler does.

Kernel: for L/M (reduced), a Kaiser-windowed sinc with cutoff at min(1/L, 1/M) (times a 0.95 roll-off so the
transition band stays below the source's Nyquist), 32 zero crossings per side at the lower rate, beta = 9
(about 90 dB stop band). scipy.signal.upfirdn applies it (the same operation as scipy.signal.resample_poly with a
custom window; no other scipy processing is used)."""
import hashlib
import math
import struct
import wave

import numpy as np
from scipy.signal import upfirdn

DESCRIPTION = ("polyphase windowed-sinc resampling (Kaiser beta 9, 32 zero crossings per side at the lower rate, "
               "cutoff 0.95 x min(fs_in, fs_out) / 2), 16-bit output by rounding, no gain change, no dither")


def read_wav(path):
    """(rate, channels, int16 array (frames, channels)) via the RIFF walk the game does (fmt, data; LIST ignored)."""
    d = open(path, "rb").read()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise ValueError("not a RIFF WAVE")
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(d):
        cid, sz = d[pos:pos + 4], struct.unpack_from("<I", d, pos + 4)[0]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", d, pos + 8)
        elif cid == b"data":
            data = d[pos + 8:pos + 8 + sz]
            break
        pos += 8 + sz + (sz & 1)
    if fmt is None or data is None:
        raise ValueError("missing fmt or data chunk")
    tag, ch, rate, _, block, bits = fmt
    if tag != 1 or bits != 16:
        raise ValueError(f"unsupported format tag {tag} / {bits} bits")
    n = len(data) // block
    pcm = np.frombuffer(data[:n * block], dtype="<i2").reshape(n, ch)
    return rate, ch, pcm, hashlib.sha256(d).hexdigest()


def kaiser_sinc(up, down, zero_crossings=32, beta=9.0, rolloff=0.95):
    """Prototype low-pass for upfirdn(up, down): gain `up`, cutoff rolloff * min(1/up, 1/down) (in cycles / sample
    of the up-sampled signal), odd length, centred."""
    cutoff = rolloff * min(1.0 / up, 1.0 / down)  # cycles per high-rate sample (Nyquist of the lower rate = 1/(2 max))
    half = int(math.ceil(zero_crossings * max(up, down)))
    n = np.arange(-half, half + 1, dtype=np.float64)
    h = cutoff * np.sinc(cutoff * n) * np.kaiser(2 * half + 1, beta)
    h *= up / h.sum()  # unity DC gain after the up-sampler's zero stuffing
    return h


def resample_pcm(pcm, rate_in, rate_out):
    """int16 (frames, channels) -> float64 (frames', channels) at rate_out (not yet rounded)."""
    if rate_in == rate_out:
        return pcm.astype(np.float64)
    g = math.gcd(rate_in, rate_out)
    up, down = rate_out // g, rate_in // g
    h = kaiser_sinc(up, down)
    half = (len(h) - 1) // 2
    n_out = int(math.ceil(pcm.shape[0] * up / down))
    out = np.empty((n_out, pcm.shape[1]), np.float64)
    for c in range(pcm.shape[1]):
        y = upfirdn(h, pcm[:, c].astype(np.float64), up=up, down=down)
        # Group delay of the centred kernel: `half` high-rate samples = half / down output samples (rounded: the
        # residual is under half an output sample).
        start = int(round(half / down))
        seg = y[start:start + n_out]
        if seg.shape[0] < n_out:
            seg = np.pad(seg, (0, n_out - seg.shape[0]))
        out[:, c] = seg
    return out


def write_wav(path, rate, pcm16):
    import os
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(pcm16.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(np.ascontiguousarray(pcm16, dtype="<i2").tobytes())


def resample_wav(src, dst, rate_out):
    rate, ch, pcm, sha = read_wav(src)
    y = resample_pcm(pcm, rate, rate_out)
    r = np.rint(y)
    clipped = int(((r > 32767) | (r < -32768)).sum())
    pcm16 = np.clip(r, -32768, 32767).astype(np.int16)
    write_wav(dst, rate_out, pcm16)
    return {"frames": int(pcm16.shape[0]), "clipped": clipped, "source_sha256": sha, "source_rate": rate}


if __name__ == "__main__":
    # Self-check: a 1 kHz tone at 22050 Hz -> 48000 Hz keeps its level and frequency; a -6 dBFS tone never clips.
    t = np.arange(22050) / 22050.0
    x = (np.sin(2 * np.pi * 1000 * t) * 16384).astype(np.int16)[:, None]
    y = resample_pcm(x, 22050, 48000)
    spec = np.abs(np.fft.rfft(y[2000:-2000, 0] * np.hanning(len(y) - 4000)))
    f = np.fft.rfftfreq(len(y) - 4000, 1 / 48000)
    print("peak at", f[spec.argmax()], "Hz; rms ratio", y[2000:-2000].std() / x[2000:-2000].std(),
          "max", np.abs(y).max(), "frames", y.shape[0], "(expected", math.ceil(22050 * 48000 / 22050), ")")
    # Alignment: an impulse stays at the same time.
    imp = np.zeros((2205, 1), np.int16)
    imp[1000] = 20000
    yi = resample_pcm(imp, 22050, 48000)
    print("impulse at", np.abs(yi[:, 0]).argmax(), "expected", round(1000 * 48000 / 22050))
