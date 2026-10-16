"""Upscalers for tools/remaster.py (numpy only). All take / return float32 RGBA in 0..1, rows top-down.

Methods (METHODS):
  nearest        pixel replication (the reference "original" look at the same size)
  scale2x        Scale2x / EPX (2x; 4x = applied twice, i.e. AdvMAME4x). Exact-colour rules, no new colours.
  xbr            xBR 2x (Hyllian's rules, as in ffmpeg's vf_xbr FILT2; 4x = applied twice). Blends only along
                 detected edges; interior pixels are copied.
  catmull        Catmull-Rom bicubic (B = 0, C = 1/2), separable.
  lanczos_clamp  Lanczos-3, separable, then every output pixel is clamped to the min / max of the 2 x 2 source texels
                 around it: no ringing or halos, no new extrema (nothing the source does not contain).
  lanczos_sharp  lanczos_clamp + a mild unsharp mask (amount 0.35, sigma 0.6 * scale), clamped the same way: edges
                 get steeper, but no pixel can leave the range of its source neighbours.

Common handling:
  * alpha bleed: colours under alpha = 0 texels are replaced by the nearest opaque colours before filtering, so
    neither this filtering nor the GPU's own bilinear sampling can pull the (usually black) key colour into edges;
  * straight (non-premultiplied) filtering of the bled image, which is equivalent to premultiplied filtering once
    the hidden colours are continuous with the edge;
  * binary alpha (ARGB1555 colour key, alpha-tested materials): the resampled alpha is thresholded at 0.5 again, so
    the key edge stays hard (the alpha test passes everything > 0) and moves to the interpolated outline;
  * edges: tiled axes (classes.json edge_u / edge_v = "wrap") filter across the seam, mirrored materials pad
    symmetrically (what D3D mirror addressing samples), clamped ones replicate the edge texel.
"""
import numpy as np

METHODS = ["nearest", "scale2x", "xbr", "catmull", "lanczos_clamp", "lanczos_sharp"]

# --- helpers ----------------------------------------------------------------------------------------------------


_PAD_MODES = {"wrap": "wrap", "mirror": "symmetric", "clamp": "edge", True: "wrap", False: "edge"}


def _pad(img, n, wrap):
    """Pad n pixels on every side per axis (wrap = (u mode, v mode), each "wrap" | "mirror" | "clamp"; booleans are
    accepted as wrap / clamp). img: (H, W, C). Mirror = D3D's mirror addressing (symmetric), clamp = edge texel."""
    out = np.pad(img, ((n, n), (0, 0), (0, 0)), mode=_PAD_MODES[wrap[1]])
    return np.pad(out, ((0, 0), (n, n), (0, 0)), mode=_PAD_MODES[wrap[0]])


def _index(idx, n, mode):
    """Source indices for one axis under the addressing mode (idx may run past 0..n-1)."""
    if mode in ("wrap", True):
        return np.mod(idx, n)
    if mode == "mirror":
        period = 2 * n
        m = np.mod(idx, period)
        return np.where(m >= n, period - 1 - m, m)
    return np.clip(idx, 0, n - 1)


def alpha_bleed(img, max_iter=64):
    """Fill the colour of alpha = 0 texels from their nearest non-transparent neighbours (iterative 3 x 3 dilation)."""
    rgb = img[..., :3].copy()
    a = img[..., 3]
    known = a > 0
    if known.all() or not known.any():
        return img
    rgb[~known] = 0
    for _ in range(max_iter):
        if known.all():
            break
        kp = np.pad(known.astype(np.float32), 1, mode="edge")
        cp = np.pad(rgb * known[..., None], ((1, 1), (1, 1), (0, 0)), mode="edge")
        ksum = np.zeros_like(kp[1:-1, 1:-1])
        csum = np.zeros_like(rgb)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                ksum += kp[1 + dy:kp.shape[0] - 1 + dy, 1 + dx:kp.shape[1] - 1 + dx]
                csum += cp[1 + dy:cp.shape[0] - 1 + dy, 1 + dx:cp.shape[1] - 1 + dx]
        fill = (~known) & (ksum > 0)
        rgb[fill] = csum[fill] / ksum[fill][:, None]
        known = known | fill
    if not known.all():
        rgb[~known] = rgb[known].mean(axis=0)
    out = img.copy()
    out[..., :3] = rgb
    return out


def _quant(img):
    """8-bit quantized copy for exact-equality tests (as the integer implementations do)."""
    return np.clip(np.rint(img * 255.0), 0, 255).astype(np.uint8)


def _finish(out, binary_alpha):
    out = np.clip(out, 0.0, 1.0)
    if binary_alpha:
        out[..., 3] = (out[..., 3] >= 0.5).astype(np.float32)
    return out.astype(np.float32)


# --- nearest ----------------------------------------------------------------------------------------------------


def nearest(img, scale):
    return np.repeat(np.repeat(img, scale, axis=0), scale, axis=1)


# --- Scale2x / EPX -----------------------------------------------------------------------------------------------


def scale2x(img, wrap):
    q = _quant(img)
    p = _pad(q, 1, wrap)
    H, W = q.shape[:2]

    def at(dy, dx):
        return p[1 + dy:1 + dy + H, 1 + dx:1 + dx + W]

    def same(x, y):
        return np.all(x == y, axis=-1)

    B, D, E, F, Hh = at(-1, 0), at(0, -1), at(0, 0), at(0, 1), at(1, 0)
    fp = _pad(img, 1, wrap)

    def fat(dy, dx):
        return fp[1 + dy:1 + dy + H, 1 + dx:1 + dx + W]

    fB, fD, fE, fF, fH = fat(-1, 0), fat(0, -1), fat(0, 0), fat(0, 1), fat(1, 0)
    bd, bf, dh, hf = same(B, D), same(B, F), same(D, Hh), same(Hh, F)
    out = np.empty((H * 2, W * 2, 4), np.float32)
    e0 = np.where((bd & ~same(B, F) & ~same(D, Hh))[..., None], fD, fE)
    e1 = np.where((bf & ~same(B, D) & ~same(F, Hh))[..., None], fF, fE)
    e2 = np.where((dh & ~same(D, B) & ~same(Hh, F))[..., None], fD, fE)
    e3 = np.where((hf & ~same(D, Hh) & ~same(B, F))[..., None], fF, fE)
    out[0::2, 0::2] = e0
    out[0::2, 1::2] = e1
    out[1::2, 0::2] = e2
    out[1::2, 1::2] = e3
    return out


# --- xBR ---------------------------------------------------------------------------------------------------------

_YUV_W = np.array([14.352, 28.176, 5.472], np.float32)  # Hyllian's luma-weighted colour distance (sums to 48)
_ALPHA_W = 48.0
_EQ_THRESHOLD = 15.0


def _df(a, b):
    d = np.abs(a - b)
    return d[..., :3] @ _YUV_W + d[..., 3] * _ALPHA_W


def xbr2x(img, wrap):
    """One xBR 2x pass. img float32 RGBA (alpha-bled)."""
    H, W = img.shape[:2]
    p = _pad(img, 2, wrap)
    qp = _pad(_quant(img), 2, wrap)

    def at(dy, dx):
        return p[2 + dy:2 + dy + H, 2 + dx:2 + dx + W]

    def qat(dy, dx):
        return qp[2 + dy:2 + dy + H, 2 + dx:2 + dx + W]

    names = {
        "PA": (-1, -1), "PB": (-1, 0), "PC": (-1, 1), "PD": (0, -1), "PE": (0, 0), "PF": (0, 1),
        "PG": (1, -1), "PH": (1, 0), "PI": (1, 1), "A1": (-2, -1), "B1": (-2, 0), "C1": (-2, 1),
        "A0": (-1, -2), "D0": (0, -2), "G0": (1, -2), "C4": (-1, 2), "F4": (0, 2), "I4": (1, 2),
        "G5": (2, -1), "H5": (2, 0), "I5": (2, 1),
    }
    F = {k: at(*v) for k, v in names.items()}
    Q = {k: qat(*v) for k, v in names.items()}
    E = [F["PE"].copy() for _ in range(4)]  # 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right

    def ne(a, b):
        return np.any(Q[a] != Q[b], axis=-1)

    def df(a, b):
        return _df(F[a], F[b])

    def eq(a, b):
        return df(a, b) < _EQ_THRESHOLD

    def blend(dst, src, k, mask):
        m = mask[..., None]
        dst[:] = np.where(m, dst + (src - dst) * k, dst)

    def filt2(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1, N0, N1, N2, N3):
        active = ne(PE, PH) & ne(PE, PF)
        e = df(PE, PC) + df(PE, PG) + df(PI, H5) + df(PI, F4) + 4 * df(PH, PF)
        i = df(PH, PD) + df(PH, I5) + df(PF, I4) + df(PF, PB) + 4 * df(PE, PI)
        active &= e <= i
        px = np.where((df(PE, PF) <= df(PE, PH))[..., None], F[PF], F[PH])
        strong = (e < i) & ((~eq(PF, PB) & ~eq(PH, PD)) | (eq(PE, PI) & ~eq(PF, I4) & ~eq(PH, I5)) | eq(PE, PG) | eq(PE, PC))
        ke, ki = df(PF, PG), df(PH, PC)
        left = (ke * 2 <= ki) & ne(PE, PG) & ne(PD, PG)
        up = (ke >= ki * 2) & ne(PE, PC) & ne(PB, PC)
        s = active & strong
        both, only_left, only_up = s & left & up, s & left & ~up, s & ~left & up
        diag = (s & ~left & ~up) | (active & ~strong)
        blend(E[N3], px, 7 / 8, both)
        blend(E[N2], px, 1 / 4, both)
        E[N1][:] = np.where(both[..., None], E[N2], E[N1])
        blend(E[N3], px, 3 / 4, only_left)
        blend(E[N2], px, 1 / 4, only_left)
        blend(E[N3], px, 3 / 4, only_up)
        blend(E[N1], px, 1 / 4, only_up)
        blend(E[N3], px, 1 / 2, diag)

    filt2("PE", "PI", "PH", "PF", "PG", "PC", "PD", "PB", "PA", "G5", "C4", "G0", "D0", "C1", "B1", "F4", "I4", "H5", "I5", "A0", "A1", 0, 1, 2, 3)
    filt2("PE", "PC", "PF", "PB", "PI", "PA", "PH", "PD", "PG", "I4", "A1", "I5", "H5", "A0", "D0", "B1", "C1", "F4", "C4", "G5", "G0", 2, 0, 3, 1)
    filt2("PE", "PA", "PB", "PD", "PC", "PG", "PF", "PH", "PI", "C1", "G0", "C4", "F4", "G5", "H5", "D0", "A0", "B1", "A1", "I5", "I4", 3, 2, 1, 0)
    filt2("PE", "PG", "PD", "PH", "PA", "PI", "PB", "PF", "PC", "A0", "I5", "A1", "B1", "I4", "F4", "H5", "G5", "D0", "G0", "C4", "C1", 1, 3, 0, 2)
    out = np.empty((H * 2, W * 2, 4), np.float32)
    out[0::2, 0::2] = E[0]
    out[0::2, 1::2] = E[1]
    out[1::2, 0::2] = E[2]
    out[1::2, 1::2] = E[3]
    return out


# --- separable resampling ------------------------------------------------------------------------------------------


def _lanczos(x, a=3):
    x = np.asarray(x, np.float64)
    out = np.zeros_like(x)
    m = np.abs(x) < a
    xm = x[m]
    out[m] = np.sinc(xm) * np.sinc(xm / a)
    return out


def _catmull(x):
    x = np.abs(np.asarray(x, np.float64))
    out = np.zeros_like(x)
    m1 = x < 1
    m2 = (x >= 1) & (x < 2)
    out[m1] = 1.5 * x[m1] ** 3 - 2.5 * x[m1] ** 2 + 1
    out[m2] = -0.5 * x[m2] ** 3 + 2.5 * x[m2] ** 2 - 4 * x[m2] + 2
    return out


def _taps(n_in, scale, kernel, support, wrap):
    """Indices (n_out, 2*support) and normalized weights for one axis."""
    n_out = n_in * scale
    x = (np.arange(n_out) + 0.5) / scale - 0.5
    base = np.floor(x).astype(np.int64)
    offs = np.arange(-support + 1, support + 1)
    idx = base[:, None] + offs[None, :]
    w = kernel(x[:, None] - idx)
    w /= w.sum(axis=1, keepdims=True)
    return _index(idx, n_in, wrap), w.astype(np.float32), base


def _resample_axis(img, axis, scale, kernel, support, wrap):
    n_in = img.shape[axis]
    idx, w, _ = _taps(n_in, scale, kernel, support, wrap)
    shape = list(img.shape)
    shape[axis] = n_in * scale
    out = np.zeros(shape, np.float32)
    for j in range(idx.shape[1]):
        g = np.take(img, idx[:, j], axis=axis)
        wj = w[:, j].reshape([-1 if k == axis else 1 for k in range(img.ndim)])
        out += g * wj
    return out


def _local_minmax(img, scale, wrap):
    """Min / max over the 2 x 2 source texels around each output pixel (bilinear footprint)."""
    def along(a_min, a_max, axis):
        n_in = a_min.shape[axis]
        mode = wrap[0] if axis == 1 else wrap[1]
        _, _, base = _taps(n_in, scale, _lanczos, 3, mode)
        i0, i1 = _index(base, n_in, mode), _index(base + 1, n_in, mode)
        lo = np.minimum(np.take(a_min, i0, axis=axis), np.take(a_min, i1, axis=axis))
        hi = np.maximum(np.take(a_max, i0, axis=axis), np.take(a_max, i1, axis=axis))
        return lo, hi

    lo, hi = along(img, img, 1)
    lo, hi = along(lo, hi, 0)
    return lo, hi


def resample(img, scale, kernel, support, wrap):
    out = _resample_axis(img, 1, scale, kernel, support, wrap[0])
    out = _resample_axis(out, 0, scale, kernel, support, wrap[1])
    return out


def _gaussian_blur(img, sigma, wrap):
    r = max(1, int(np.ceil(sigma * 3)))
    x = np.arange(-r, r + 1)
    k = np.exp(-0.5 * (x / sigma) ** 2).astype(np.float32)
    k /= k.sum()
    p = _pad(img, r, wrap)
    H, W = img.shape[:2]
    tmp = np.zeros((H + 2 * r, W, img.shape[2]), np.float32)
    for j, kj in enumerate(k):
        tmp += kj * p[:, j:j + W]
    out = np.zeros_like(img)
    for j, kj in enumerate(k):
        out += kj * tmp[j:j + H]
    return out


# --- entry point -----------------------------------------------------------------------------------------------


def upscale(img, scale, method, wrap=("clamp", "clamp"), binary_alpha=False):
    """img: float32 RGBA (H, W, 4) 0..1; wrap = (u mode, v mode) in "wrap" | "mirror" | "clamp" (see _pad).
    Returns (H*scale, W*scale, 4)."""
    img = np.asarray(img, np.float32)
    if method == "nearest":
        return _finish(nearest(img, scale), binary_alpha)
    src = alpha_bleed(img)
    if method in ("scale2x", "xbr"):
        if scale not in (2, 4):
            raise ValueError(f"{method} supports 2x and 4x")
        fn = scale2x if method == "scale2x" else xbr2x
        out = fn(src, wrap)
        if scale == 4:
            out = fn(out, wrap)
        return _finish(out, binary_alpha)
    if method == "catmull":
        return _finish(resample(src, scale, _catmull, 2, wrap), binary_alpha)
    if method in ("lanczos_clamp", "lanczos_sharp"):
        out = resample(src, scale, _lanczos, 3, wrap)
        lo, hi = _local_minmax(src, scale, wrap)
        out = np.clip(out, lo, hi)
        if method == "lanczos_sharp":
            blur = _gaussian_blur(out, 0.6 * scale, wrap)
            out[..., :3] += 0.35 * (out[..., :3] - blur[..., :3])
            out = np.clip(out, lo, hi)
        return _finish(out, binary_alpha)
    raise ValueError(f"unknown method {method}")
