"""Build a synthetic consecutive sequence (E2SEQ02) by panning one recorded frame.

The offline fixture can replay recorded consecutive bursts through the real neural
pass (build-replay.cmd, ETS2_REPLAY_SEQUENCE). Until real driving bursts exist, this
makes a controlled one from a captured VR frame: each eye's colour, depth and mask
are shifted by a constant number of pixels per frame (edge pixels replicated), the
motion plane carries the exactly matching vectors in the feeder's convention
(current position + vector = previous position, in native pixels), and the pixels
that entered from the replicated edge are marked distrusted in the mask.

It exercises the temporal path with exact vectors and a real model. It is not a
substitute for a real burst: no parallax, no lighting change, no moving objects.

  python tools/make_pan_sequence.py --scene E:\\ETS2-DLSS5-Lab\\20260907\\public-showcase-v02\\scenes\\cab ^
      --out E:\\ETS2-DLSS5-Lab\\preview-next\\sequences\\cab-pan-x6.sequence --frames 48 --dx 6 --dy 0
"""
from pathlib import Path
import argparse, hashlib, json, struct, sys
import numpy as np

HEADER = struct.Struct('<8s6I')
FRAME = struct.Struct('<4Q2I')


def shifted(plane, k_dx, k_dy, eye_width):
    """Shift each eye's content by (k_dx, k_dy) pixels with edge replication."""
    h, w = plane.shape[:2]
    out = np.empty_like(plane)
    for eye in (0, 1):
        x0 = eye * eye_width
        src = plane[:, x0:x0 + eye_width]
        ys = np.clip(np.arange(h) - k_dy, 0, h - 1)
        xs = np.clip(np.arange(eye_width) - k_dx, 0, eye_width - 1)
        out[:, x0:x0 + eye_width] = src[ys][:, xs]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--scene', required=True, help='lab scene folder with original.raw, depth.raw, mask.raw and scene.json')
    ap.add_argument('--out', required=True, help='destination .sequence file (a .json manifest is written beside it)')
    ap.add_argument('--frames', type=int, default=48)
    ap.add_argument('--dx', type=float, default=6.0, help='horizontal pan per frame in native pixels (positive: content moves right)')
    ap.add_argument('--dy', type=float, default=0.0)
    ap.add_argument('--interval-ms', type=float, default=30.0, help='nominal frame interval written into the headers')
    ap.add_argument('--mv-noise', type=float, default=0.0, help='standard deviation, in pixels, of smooth per-frame errors added to the vectors in 32 px blocks (emulates optical-flow error)')
    ap.add_argument('--mv-drop', type=float, default=0.0, help='fraction of 32 px blocks per frame whose vectors are zeroed and masked (emulates validation rejecting flow)')
    ap.add_argument('--luma-ramp', type=float, default=0.0, help='total brightness change in percent across the sequence, applied to the colour plane (emulates a lighting change)')
    ap.add_argument('--luma-flicker', type=float, default=0.0, help='alternating brightness change in percent, +p on even frames and -p on odd frames (a pure temporal flicker to test history filters)')
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    if not 2 <= a.frames <= 48:
        raise SystemExit('frames must be 2..48 (replay limit)')
    scene = Path(a.scene)
    info = json.loads((scene / 'scene.json').read_text(encoding='utf-8'))
    w, h = info['width'], info['height']
    ew = w // 2
    original = np.memmap(scene / 'original.raw', dtype=np.uint8, mode='r', shape=(h, w, 4))
    depth = np.memmap(scene / 'depth.raw', dtype='<f4', mode='r', shape=(h, w))
    mask0 = np.memmap(scene / 'mask.raw', dtype=np.uint8, mode='r', shape=(h, w))
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        raise SystemExit(f'refusing to overwrite {out}')
    plane_bytes = w * h * 13
    freq = 10_000_000
    qpc = 1_000_000_000
    rng = np.random.default_rng(a.seed)
    bh, bw = (h + 31) // 32, (w + 31) // 32
    digest = hashlib.sha256()
    partial = out.with_suffix(out.suffix + '.part')
    with partial.open('wb') as f:
        header = HEADER.pack(b'E2SEQ02\0', w, h, a.frames, 4, 2, HEADER.size)
        f.write(header); digest.update(header)
        for k in range(a.frames):
            kx, ky = int(round(k * a.dx)), int(round(k * a.dy))
            color = shifted(np.asarray(original), kx, ky, ew)
            gain = 1.0
            if a.luma_ramp:
                gain *= 1.0 + a.luma_ramp / 100.0 * (k / max(1, a.frames - 1))
            if a.luma_flicker:
                gain *= 1.0 + a.luma_flicker / 100.0 * (1 if k % 2 == 0 else -1)
            if gain != 1.0:
                color = color.copy()
                color[..., :3] = np.clip(color[..., :3].astype(np.float32) * gain + 0.5, 0, 255).astype(np.uint8)
            z = shifted(np.asarray(depth), kx, ky, ew)
            mask = shifted(np.asarray(mask0), kx, ky, ew).copy()
            motion = np.empty((h, w, 2), dtype='<f2')
            motion[..., 0] = -a.dx
            motion[..., 1] = -a.dy
            if a.mv_noise > 0:
                noise = rng.normal(0.0, a.mv_noise, size=(bh, bw, 2)).astype(np.float32)
                full = np.repeat(np.repeat(noise, 32, axis=0), 32, axis=1)[:h, :w]
                motion = (motion.astype(np.float32) + full).astype('<f2')
            if a.mv_drop > 0:
                drop = rng.random((bh, bw)) < a.mv_drop
                full = np.repeat(np.repeat(drop, 32, axis=0), 32, axis=1)[:h, :w]
                motion[full] = 0
                mask[full] = 255
            # Pixels that came from a replicated edge have no true previous position.
            for eye in (0, 1):
                x0 = eye * ew
                if kx > 0:
                    mask[:, x0:x0 + min(kx, ew)] = 255
                elif kx < 0:
                    mask[:, x0 + max(0, ew + kx):x0 + ew] = 255
                if ky > 0:
                    mask[:min(ky, h), x0:x0 + ew] = 255
                elif ky < 0:
                    mask[max(0, h + ky):, x0:x0 + ew] = 255
            flags = 0
            fh = FRAME.pack(1000 + k, 5000 + k, qpc + int(k * a.interval_ms * freq / 1000), freq, flags, plane_bytes)
            f.write(fh); digest.update(fh)
            for plane in (color, z, motion, mask):
                b = np.ascontiguousarray(plane).tobytes()
                f.write(b); digest.update(b)
            print(f'frame {k}: shift ({kx},{ky})', flush=True)
    expected = HEADER.size + a.frames * (FRAME.size + plane_bytes)
    if partial.stat().st_size != expected:
        raise SystemExit('length mismatch')
    partial.replace(out)
    manifest = {'schema': 2, 'synthetic': True, 'method': f'constant pan of one recorded frame by ({a.dx},{a.dy}) native px per frame, edge replicated, exact vectors, entering pixels masked',
                'mv_noise_px': a.mv_noise, 'mv_drop_fraction': a.mv_drop, 'luma_ramp_percent': a.luma_ramp, 'luma_flicker_percent': a.luma_flicker, 'seed': a.seed,
                'scene': str(scene), 'source_frame': info.get('source_frame'), 'file': str(out), 'sha256': digest.hexdigest(), 'bytes': expected,
                'width': w, 'height': h, 'frames': a.frames, 'dx': a.dx, 'dy': a.dy, 'interval_ms': a.interval_ms,
                'initialization': 'Reset every neural context at the first replayed sample; preserve history thereafter.'}
    out.with_suffix(out.suffix + '.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps({k: manifest[k] for k in ('file', 'frames', 'dx', 'dy', 'bytes', 'sha256')}, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
