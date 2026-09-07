"""Offline stability metrics for recorded Feeder captures (branch milestone 0).

Reads a DLSS5-Captures folder (four spaced comparison frames, schema 1) or a
DLSS5-Motion-Captures folder (a consecutive burst, schema 2) written by the
feeder, and reports three families of numbers in 8-bit code values (0..255):

  effect     mean |residual| inside the processed square, split into a low band
             (8x8 box average, then a 3x3 box at 1/8 resolution; about 24 px of
             support at native resolution) and the remaining high band.
  flicker    consecutive bursts only. Mean absolute change of the residual after
             warping the previous frame's residual with the current frame's
             recorded motion (current-to-previous, pixels), per band. Pixels are
             excluded when the warp leaves the eye, the recorded mask distrusts
             the vector, or depth disagrees by more than 2% (the same gate the
             stability filter uses). Frames flagged as a common neural reset are
             reported but left out of the summary.
  binocular  mean |left - right| of the low-band residual at 1/8 resolution
             after aligning the eyes for far content. The two eye buffers are
             asymmetric frusta: a distant point lands about 600 px further left
             in the right eye than in the left eye at 2504 px per eye. The shift
             is measured per capture by correlating the high-passed luminance of
             the world (raw depth below --cab-depth) between the eyes, and the
             right eye is then read at that offset. Cells on the cab side of
             --cab-depth in either eye, and cells outside both eyes' processed
             squares, are excluded. ETS2 draws the cabin in its own compressed
             depth band (raw 0.90..0.94, reversed) and the world below 0.05 with
             sky at 0, so 0.5 separates cab from world exactly. The unaligned
             score and the fraction of each square that has no processed
             counterpart in the other eye are reported beside it.

With --logs <install folder> it also summarises cost from dlss5-feed.log (600
frame windows), depth-match.log (comparison_ms) and feeder-profile.jsonl.

These are change measurements, not perceptual scores. A lower flicker number
can come from a weaker effect, so effect magnitude is always reported beside it.
Requires numpy. Never writes into the capture folder.
"""
from pathlib import Path
import argparse, json, re, statistics
import numpy as np

KINDS = ('original', 'depth', 'motion', 'mask', 'result')


def load_frames(folder):
    folder = Path(folder)
    manifests = sorted(folder.glob('frame-*.json'), key=lambda p: int(re.search(r'frame-(\d+)', p.name).group(1)))
    if not manifests:
        raise SystemExit(f'no frame-*.json in {folder}')
    frames = []
    for m in manifests:
        p = json.loads(m.read_text(encoding='utf-8'))
        images = {i['kind']: i for i in p['images']}
        missing = [k for k in KINDS if k not in images]
        if missing:
            raise SystemExit(f'{m.name} lacks planes {missing}')
        p['_images'] = images
        frames.append(p)
    session_path = folder / ('sequence.json' if (folder / 'sequence.json').exists() else 'session.json')
    session = json.loads(session_path.read_text(encoding='utf-8')) if session_path.exists() else {}
    consecutive = bool(session.get('temporal_sequence')) and all(
        frames[i]['frame_id'] == frames[i - 1]['frame_id'] + 1 for i in range(1, len(frames)))
    return folder, session, frames, consecutive


def plane(folder, p, kind):
    i = p['_images'][kind]
    w, h = i['width'], i['height']
    if kind in ('original', 'result'):
        return np.memmap(folder / i['file'], mode='r', dtype=np.uint8, shape=(h, w, 4))
    if kind == 'motion':
        return np.memmap(folder / i['file'], mode='r', dtype='<f2', shape=(h, w, 2))
    if kind == 'mask':
        return np.memmap(folder / i['file'], mode='r', dtype=np.uint8, shape=(h, w))
    return np.memmap(folder / i['file'], mode='r', dtype='<f4', shape=(h, w))


def sample(a, x, y):
    """Bilinear sample at pixel-centre coordinates; the caller masks the borders."""
    h, w = a.shape[:2]
    x = np.clip(x, 0, w - 1); y = np.clip(y, 0, h - 1)
    ix = np.floor(x).astype(np.int32); iy = np.floor(y).astype(np.int32)
    jx = np.minimum(ix + 1, w - 1); jy = np.minimum(iy + 1, h - 1)
    fx = (x - ix).astype(np.float32); fy = (y - iy).astype(np.float32)
    if a.ndim == 3:
        fx = fx[..., None]; fy = fy[..., None]
    return (a[iy, ix] * (1 - fx) + a[iy, jx] * fx) * (1 - fy) + (a[jy, ix] * (1 - fx) + a[jy, jx] * fx) * fy


def box8(a, reduce='mean'):
    """8x8 block reduction; trailing rows/columns that do not fill a block are dropped."""
    h8, w8 = a.shape[0] // 8, a.shape[1] // 8
    blocks = a[:h8 * 8, :w8 * 8].reshape(h8, 8, w8, 8, *a.shape[2:])
    return blocks.max(axis=(1, 3)) if reduce == 'max' else blocks.mean(axis=(1, 3))


def blur3(a):
    pad = np.pad(a, ((1, 1), (1, 1)) + ((0, 0),) * (a.ndim - 2), mode='edge')
    out = np.zeros_like(a)
    for dy in (0, 1, 2):
        for dx in (0, 1, 2):
            out += pad[dy:dy + a.shape[0], dx:dx + a.shape[1]]
    return out / 9.0


def low_band8(r):
    """8x8 box average followed by a 3x3 box at 1/8 resolution."""
    return blur3(box8(r))


def upsample8(low8, xs, ys):
    """Bilinear reconstruction of the 1/8 field at native pixel coordinates."""
    return sample(low8, (xs + 0.5) / 8.0 - 0.5, (ys + 0.5) / 8.0 - 0.5)


def high_pass(a, radius=3):
    pad = np.pad(a, radius, mode='edge')
    n = 2 * radius + 1
    mean = sum(pad[dy:dy + a.shape[0], dx:dx + a.shape[1]] for dy in range(n) for dx in range(n)) / (n * n)
    return a - mean


def eye_shift(lum_l8, lum_r8, world_l8, world_r8, dxs=range(-110, 1), dys=range(-3, 4)):
    """Horizontal/vertical cell offset that best aligns far content: L[y, x] ~ R[y + dy, x + dx]."""
    a_all, b_all = high_pass(lum_l8), high_pass(lum_r8)
    H, W = a_all.shape
    best = (-2.0, 0, 0, 0)
    for dy in dys:
        for dx in dxs:
            ys = slice(max(0, -dy), min(H, H - dy)); xs = slice(max(0, -dx), min(W, W - dx))
            ys2 = slice(max(0, dy), min(H, H + dy)); xs2 = slice(max(0, dx), min(W, W + dx))
            m = world_l8[ys, xs] & world_r8[ys2, xs2]
            if m.sum() < 1500:
                continue
            a = a_all[ys, xs][m]; b = b_all[ys2, xs2][m]
            a = a - a.mean(); b = b - b.mean()
            r = float((a * b).sum() / np.sqrt((a * a).sum() * (b * b).sum() + 1e-9))
            if r > best[0]:
                best = (r, dx, dy, int(m.sum()))
    return best


def shifted_pair(a_l, a_r, dx, dy):
    """Overlapping views of two 1/8 fields so that a_l[y, x] pairs with a_r[y + dy, x + dx]."""
    H, W = a_l.shape[:2]
    ys = slice(max(0, -dy), min(H, H - dy)); xs = slice(max(0, -dx), min(W, W - dx))
    ys2 = slice(max(0, dy), min(H, H + dy)); xs2 = slice(max(0, dx), min(W, W + dx))
    return a_l[ys, xs], a_r[ys2, xs2]


def crop_rect(meta, ew, h, eye=0):
    """Processed square of one eye in native per-eye pixels (x0, y0, x1, y1)."""
    ww, wh = meta.get('work_width'), meta.get('work_height')
    if not ww or not wh or not meta.get('neural_eye_width'):
        return (0, 0, ew, h)
    sx = ew / (ww / 2.0); sy = h / float(wh)
    crop_x = meta.get('crop_x_left' if eye == 0 else 'crop_x_right', meta['crop_x'])
    x0 = int(round(crop_x * sx)); y0 = int(round(meta['crop_y'] * sy))
    x1 = int(round((crop_x + meta['neural_eye_width']) * sx))
    y1 = int(round((meta['crop_y'] + meta['neural_eye_height']) * sy))
    return (max(0, x0), max(0, y0), min(ew, x1), min(h, y1))


def common_reset(p):
    meta = p['metadata']
    if meta.get('reset'):
        return True
    slots = [v for v in meta.get('observer', {}).get('actual_nr_parameters', []) if v]
    resets = [v.get('DLSSNR.Reset') for v in slots]
    return bool(resets) and all(v == 1 for v in resets)


def nr_settings(p):
    slots = [v for v in p['metadata'].get('observer', {}).get('actual_nr_parameters', []) if v]
    if not slots:
        return None
    s = slots[0]
    return {k.replace('DLSSNR.', ''): s.get(k) for k in ('DLSSNR.Intensity', 'DLSSNR.Style', 'DLSSNR.LocalToneStrength',
                                                          'DLSSNR.LocalStructureStrength', 'preset')}


def analyze(folder, stride=2, cab_depth=0.5, margin=32):
    folder, session, frames, consecutive = load_frames(folder)
    w, h = frames[0]['_images']['original']['width'], frames[0]['_images']['original']['height']
    ew = w // 2
    rows, previous, previous_color = [], None, None
    yy, xx = np.mgrid[0:h:stride, 0:ew:stride].astype(np.float32)
    depth_percentiles = None
    shift = None
    for n, p in enumerate(frames):
        meta = p['metadata']
        color = plane(folder, p, 'original'); output = plane(folder, p, 'result')
        motion = plane(folder, p, 'motion'); mask = plane(folder, p, 'mask'); depth = plane(folder, p, 'depth')
        reset = common_reset(p)
        rects = [crop_rect(meta, ew, h, e) for e in (0, 1)]
        per_eye, colors, low8s, near8s, lum8s = [], [], [], [], []
        for eye in (0, 1):
            cx0, cy0, cx1, cy1 = rects[eye]
            sl = slice(eye * ew, (eye + 1) * ew)
            c = np.asarray(color[:, sl, :3], np.float32); o = np.asarray(output[:, sl, :3], np.float32)
            r = o - c
            d = np.asarray(depth[:, sl], np.float32)
            low8 = low_band8(r)
            low = upsample8(low8, xx, yy)
            rs = r[::stride, ::stride]
            high = rs - low
            inside = (xx >= cx0) & (xx < cx1) & (yy >= cy0) & (yy < cy1)
            if depth_percentiles is None:
                depth_percentiles = {str(q): float(v) for q, v in zip((1, 5, 25, 50, 75, 95, 99), np.percentile(d[::4, ::4], (1, 5, 25, 50, 75, 95, 99)))}
            row = {'index': n, 'frame_id': p['frame_id'], 'eye': eye, 'common_reset': reset,
                   'effect_square_total': float(np.abs(rs[inside]).mean()),
                   'effect_square_low': float(np.abs(low[inside]).mean()),
                   'effect_square_high': float(np.abs(high[inside]).mean()),
                   'effect_eye_total': float(np.abs(rs).mean()),
                   'square_fraction_of_eye': float(inside.mean())}
            if consecutive and previous is not None:
                pr, plow8, pd = previous[eye]
                mv = np.asarray(motion[:, sl, :], np.float32)[::stride, ::stride]
                px = xx + mv[..., 0]; py = yy + mv[..., 1]
                in_bounds = (xx >= margin) & (xx < ew - margin) & (yy >= margin) & (yy < h - margin) & \
                            (px >= margin) & (px < ew - margin) & (py >= margin) & (py < h - margin) & inside
                distrust = np.asarray(mask[:, sl], np.float32)[::stride, ::stride] > 127
                ds = d[::stride, ::stride]
                wd = sample(pd, px, py)
                depth_ok = np.abs(ds - wd) <= 0.02 * np.maximum(np.abs(ds), np.abs(wd)) + 1e-5
                gated = in_bounds & ~distrust & depth_ok
                wr = sample(pr, px, py)
                wlow = upsample8(plow8, px, py)
                whigh = wr - wlow
                raw_change = np.abs(rs - pr[::stride, ::stride])
                row.update(in_bounds_fraction=float(in_bounds.mean()), gated_fraction=float(gated.mean()),
                           raw_change_total=float(raw_change[inside].mean()),
                           flicker_in_bounds_low=float(np.abs(low - wlow)[in_bounds].mean()) if in_bounds.any() else None,
                           flicker_in_bounds_high=float(np.abs(high - whigh)[in_bounds].mean()) if in_bounds.any() else None,
                           flicker_gated_low=float(np.abs(low - wlow)[gated].mean()) if gated.any() else None,
                           flicker_gated_high=float(np.abs(high - whigh)[gated].mean()) if gated.any() else None,
                           flicker_gated_total=float(np.abs(rs - wr)[gated].mean()) if gated.any() else None,
                           source_warp_error=float(np.abs(c[::stride, ::stride] - sample(previous_color[eye], px, py))[gated].mean()) if gated.any() else None,
                           input_interval_ms=((p['input_qpc'] - frames[n - 1]['input_qpc']) * 1000.0 / session['qpc_frequency']) if 'input_qpc' in p and session.get('qpc_frequency') else None)
            rows.append(row)
            per_eye.append((r, low8, d)); colors.append(c)
            low8s.append(low8); near8s.append(box8(d, 'max'))
            lum8s.append(box8(0.299 * c[..., 0] + 0.587 * c[..., 1] + 0.114 * c[..., 2]))
        # eye alignment for far content, measured once per capture on the world region
        world = [(nd < cab_depth) if cab_depth is not None else np.ones_like(nd, bool) for nd in near8s]
        if shift is None:
            corr, dx, dy, cells = eye_shift(lum8s[0], lum8s[1], world[0], world[1])
            zero = eye_shift(lum8s[0], lum8s[1], world[0], world[1], dxs=[0], dys=[0])[0]
            shift = {'dx_cells': dx, 'dy_cells': dy, 'dx_px_native': dx * 8, 'correlation': corr, 'correlation_unshifted': zero, 'cells': cells}
        dx, dy = shift['dx_cells'], shift['dy_cells']
        h8, w8 = low8s[0].shape[:2]
        y8, x8 = np.mgrid[0:h8, 0:w8]
        squares8 = [(x8 * 8 >= r[0]) & (x8 * 8 + 8 <= r[2]) & (y8 * 8 >= r[1]) & (y8 * 8 + 8 <= r[3]) for r in rects]
        # aligned comparison: left cell (y, x) against right cell (y + dy, x + dx)
        l_low, r_low = shifted_pair(low8s[0], low8s[1], dx, dy)
        l_sq, r_sq = shifted_pair(squares8[0], squares8[1], dx, dy)
        l_world, r_world = shifted_pair(world[0], world[1], dx, dy)
        both_squares = l_sq & r_sq
        included = both_squares & l_world & r_world
        diff = np.abs(l_low - r_low).mean(axis=-1)
        magnitude = 0.5 * (np.abs(l_low).mean(axis=-1) + np.abs(r_low).mean(axis=-1))
        # unaligned reference and the part of the left square without a processed counterpart
        u_diff = np.abs(low8s[0] - low8s[1]).mean(axis=-1)
        u_incl = squares8[0] & squares8[1] & world[0] & world[1]
        unmatched = 1.0 - float(both_squares.sum() / max(1, squares8[0].sum()))
        rows.append({'index': n, 'frame_id': p['frame_id'], 'eye': 'both', 'common_reset': reset,
                     'binocular_low': float(diff[included].mean()) if included.any() else None,
                     'binocular_low_relative': float(diff[included].mean() / (magnitude[included].mean() + 1e-6)) if included.any() else None,
                     'binocular_low_unaligned': float(u_diff[u_incl].mean()) if u_incl.any() else None,
                     'binocular_included_cells': int(included.sum()),
                     'square_unmatched_fraction': unmatched})
        previous = per_eye
        previous_color = colors

    def summarize(keys, select):
        out = {}
        for key in keys:
            values = [r[key] for r in rows if select(r) and r.get(key) is not None]
            out[key] = {'mean': float(np.mean(values)), 'median': float(np.median(values)),
                        'p95': float(np.percentile(values, 95)), 'n': len(values)} if values else None
        return out
    summary = {
        'eyes': summarize(('effect_square_total', 'effect_square_low', 'effect_square_high', 'effect_eye_total'), lambda r: r['eye'] != 'both'),
        'binocular': summarize(('binocular_low', 'binocular_low_relative', 'binocular_low_unaligned', 'binocular_included_cells', 'square_unmatched_fraction'), lambda r: r['eye'] == 'both'),
    }
    if consecutive:
        summary['flicker_excluding_resets'] = summarize(
            ('flicker_gated_low', 'flicker_gated_high', 'flicker_gated_total', 'flicker_in_bounds_low', 'flicker_in_bounds_high',
             'raw_change_total', 'gated_fraction', 'source_warp_error', 'input_interval_ms'),
            lambda r: r['eye'] != 'both' and r['index'] > 0 and not r['common_reset'])
    return {'schema': 2, 'source': str(folder.resolve()), 'frames': len(frames), 'consecutive': consecutive,
            'native_extent': [w, h], 'stride': stride, 'cab_depth_threshold': cab_depth, 'eye_shift': shift,
            'processed_square_native': [list(crop_rect(frames[0]['metadata'], ew, h, e)) for e in (0, 1)],
            'metadata_first_frame': {k: v for k, v in frames[0]['metadata'].items() if k not in ('observer', 'guide_uniforms')},
            'neural_settings': nr_settings(frames[0]), 'depth_percentiles_raw_first_frame': depth_percentiles,
            'units': '8-bit code values; pixels for motion; raw reversed-Z for depth; 1/8-resolution cells for eye shift', 'summary': summary, 'rows': rows}


def parse_logs(install):
    install = Path(install)
    out = {}
    feed = install / 'dlss5-feed.log'
    if feed.exists():
        wins = []
        for line in feed.read_text(encoding='utf-8', errors='replace').splitlines():
            m = re.search(r'600 frames: feed CPU ([\d.]+) ms/frame \| frame interval ([\d.]+) ms \(([\d.]+) fps\).*?feed GPU ([\d.]+) ms/frame.*?worst frame ([\d.]+) ms.*stalls (\d+)', line)
            if m:
                wins.append(dict(zip(('feed_cpu_ms', 'interval_ms', 'fps', 'feed_gpu_ms', 'worst_ms', 'stalls'), map(float, m.groups()))))
        if wins:
            out['feed_windows'] = len(wins)
            for key in ('interval_ms', 'fps', 'feed_gpu_ms', 'feed_cpu_ms', 'worst_ms', 'stalls'):
                v = [w[key] for w in wins]
                out[key] = {'median': statistics.median(v), 'min': min(v), 'max': max(v)}
            clean = [w for w in wins if w['stalls'] == 0]
            if clean:
                out['clean_windows_interval_ms'] = statistics.median([w['interval_ms'] for w in clean])
    dm = install / 'depth-match.log'
    if dm.exists():
        cm = [float(x) for x in re.findall(r'comparison_ms=([\d.]+)', dm.read_text(encoding='utf-8', errors='replace'))]
        if cm:
            cm.sort()
            out['comparison_ms'] = {'median': statistics.median(cm), 'p5': cm[len(cm) // 20], 'p95': cm[len(cm) * 19 // 20], 'n': len(cm)}
    prof = install / 'feeder-profile.jsonl'
    if prof.exists():
        keys = ('feeder_path_elapsed_ms', 'neural_list_gpu_ms', 'input_gpu_ms', 'handoff_elapsed_ms', 'output_gpu_ms', 'filter_gpu_ms')
        acc = {k: [] for k in keys}
        for line in prof.read_text(encoding='utf-8', errors='replace').splitlines():
            try:
                row = json.loads(line)
            except ValueError:
                continue
            if row.get('type') == 'profile_header' or not row.get('successful_delivery') or not row.get('timestamps_valid'):
                continue
            for k in keys:
                if isinstance(row.get(k), (int, float)):
                    acc[k].append(float(row[k]))
        out['profile'] = {k: {'median': statistics.median(v), 'p95': sorted(v)[len(v) * 19 // 20], 'n': len(v)} for k, v in acc.items() if v}
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folders', nargs='*', help='capture folders (DLSS5-Captures/<id> or DLSS5-Motion-Captures/<id>)')
    ap.add_argument('--logs', help='installed preview folder whose logs give the cost baseline')
    ap.add_argument('--stride', type=int, default=2)
    ap.add_argument('--cab-depth', type=float, default=0.5, help='raw depth separating the cab band (greater) from the world (smaller); -1 disables the exclusion')
    ap.add_argument('--output', help='write the full JSON here')
    a = ap.parse_args()
    if not 1 <= a.stride <= 16:
        raise SystemExit('stride must be 1..16')
    cab = None if a.cab_depth is not None and a.cab_depth < 0 else a.cab_depth
    result = {'captures': [analyze(f, a.stride, cab) for f in a.folders]}
    if a.logs:
        result['cost'] = parse_logs(a.logs)
    if a.output:
        Path(a.output).write_text(json.dumps(result, indent=1), encoding='utf-8')
    for cap in result['captures']:
        s = cap['summary']
        print(f"{Path(cap['source']).name}: frames={cap['frames']} consecutive={cap['consecutive']} square={cap['processed_square_native']} nr={cap['neural_settings']}")
        sh = cap['eye_shift']
        print(f"  eye shift for far content: {sh['dx_px_native']} px native ({sh['dx_cells']},{sh['dy_cells']} cells), corr {sh['correlation']:.2f} vs {sh['correlation_unshifted']:.2f} unshifted")
        e = s['eyes']
        print(f"  effect in square: total {e['effect_square_total']['mean']:.2f}  low {e['effect_square_low']['mean']:.2f}  high {e['effect_square_high']['mean']:.2f}  (whole eye {e['effect_eye_total']['mean']:.2f})")
        b = s['binocular']
        if b['binocular_low']:
            print(f"  binocular low-band |L-R| aligned: {b['binocular_low']['mean']:.2f} (relative {b['binocular_low_relative']['mean']:.2f}); unaligned {b['binocular_low_unaligned']['mean']:.2f}; square without counterpart in other eye {b['square_unmatched_fraction']['mean']:.2f}")
        f = s.get('flicker_excluding_resets')
        if f and f.get('flicker_gated_low'):
            print(f"  flicker (motion compensated, gated): low {f['flicker_gated_low']['mean']:.2f}  high {f['flicker_gated_high']['mean']:.2f}  total {f['flicker_gated_total']['mean']:.2f}; raw change {f['raw_change_total']['mean']:.2f}; gated fraction {f['gated_fraction']['mean']:.2f}; source warp error {f['source_warp_error']['mean']:.2f}")
    if 'cost' in result:
        print('cost:', json.dumps(result['cost'], indent=None))


if __name__ == '__main__':
    main()
