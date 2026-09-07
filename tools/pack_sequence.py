"""Verify a recorded consecutive burst and pack it into a replayable E2SEQ02 sequence.

The feeder's motion test writes DLSS5-Motion-Captures/<id>/ with sequence.json and
frame-N.json plus raw planes. This checks the manifests are consecutive and consistent,
then writes the four input planes of every frame into one file the replay feeder
(build-replay.cmd, ETS2_REPLAY_SEQUENCE) can drive through the real neural pass.
The recorded outputs are not packed; replaying reproduces them with whatever build
and settings the run uses. The source folder is never modified.

  python tools/pack_sequence.py E:\\ETS2-VR-Preview\\DLSS5-Motion-Captures\\<id> --out E:\\...\\bursts\\name.sequence
"""
from pathlib import Path
import argparse, hashlib, json, struct, sys
import numpy as np

HEADER = struct.Struct('<8s6I')
FRAME = struct.Struct('<4Q2I')
KINDS = ('original', 'depth', 'motion', 'mask')
FORMATS = ((27, 28, 29), (41,), (34,), (61,))
BPP = (4, 4, 4, 1)


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def common_reset(packet):
    meta = packet['metadata']
    slots = [v for v in meta.get('observer', {}).get('actual_nr_parameters', []) if v and v['pass'] <= meta.get('passes_per_eye', 1)]
    resets = [v.get('DLSSNR.Reset') for v in slots]
    if all(v is not None for v in resets) and len(set(bool(v) for v in resets)) > 1:
        raise ValueError('unequal eye/pass resets cannot be replayed as a common reset')
    return bool(meta.get('reset')) or bool(resets and all(v == 1 for v in resets))


def verify(folder, hashes=True):
    folder = Path(folder).resolve()
    session = json.loads((folder / 'sequence.json').read_text(encoding='utf-8'))
    if session.get('schema') != 2 or not session.get('complete') or not session.get('temporal_sequence'):
        raise ValueError('a complete consecutive sequence is required')
    n, freq = session['written'], session['qpc_frequency']
    if not isinstance(n, int) or not 2 <= n <= 48 or n != session['submitted'] or not freq > 0:
        raise ValueError('invalid sequence length or frequency')
    packets, extent, prev = [], None, None
    for idx in range(n):
        p = json.loads((folder / f'frame-{idx}.json').read_text(encoding='utf-8'))
        if p.get('schema') != 2 or not p.get('complete') or p.get('index') != idx or p.get('layout') != 'side_by_side':
            raise ValueError(f'frame {idx}: invalid header')
        if prev and (p['frame_id'] != prev['frame_id'] + 1 or p['callback_id'] != prev['callback_id'] + 1 or p['input_qpc'] <= prev['input_qpc']):
            raise ValueError(f'frame {idx}: frame, callback or timestamp discontinuity')
        images = {i['kind']: i for i in p['images']}
        for k, formats, bpp in zip(KINDS, FORMATS, BPP):
            i = images[k]; w, h = i['width'], i['height']
            if extent is None:
                extent = (w, h)
            if (w, h) != extent or i['format'] not in formats or i['bytes_per_pixel'] != bpp or i['row_bytes'] != w * bpp or i['bytes'] != w * h * bpp:
                raise ValueError(f'frame {idx}: {k} plane contract mismatch')
            path = (folder / i['file']).resolve()
            if path.parent != folder or path.stat().st_size != i['bytes']:
                raise ValueError(f'frame {idx}: {k} path or size invalid')
            if hashes and sha(path) != i['sha256']:
                raise ValueError(f'frame {idx}: {k} checksum mismatch')
            if k in ('depth', 'motion'):
                a = np.memmap(path, dtype='<f4' if k == 'depth' else '<f2', mode='r')
                if not np.isfinite(a).all():
                    raise ValueError(f'frame {idx}: nonfinite {k} data')
        p['_images'] = images
        packets.append(p); prev = p
    return session, packets, extent


def pack(folder, destination):
    folder = Path(folder).resolve(); destination = Path(destination).resolve()
    if destination.exists():
        raise ValueError(f'refusing to overwrite {destination}')
    session, packets, (w, h) = verify(folder)
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + '.part')
    plane_bytes = w * h * 13
    digest = hashlib.sha256()
    with partial.open('wb') as f:
        header = HEADER.pack(b'E2SEQ02\0', w, h, len(packets), 4, 2, HEADER.size)
        f.write(header); digest.update(header)
        for p in packets:
            fh = FRAME.pack(p['frame_id'], p['callback_id'], p['input_qpc'], session['qpc_frequency'], int(common_reset(p)), plane_bytes)
            f.write(fh); digest.update(fh)
            for kind in KINDS:
                with (folder / p['_images'][kind]['file']).open('rb') as src:
                    while chunk := src.read(8 * 1024 * 1024):
                        f.write(chunk); digest.update(chunk)
    expected = HEADER.size + len(packets) * (FRAME.size + plane_bytes)
    if partial.stat().st_size != expected:
        raise ValueError('packed length mismatch')
    partial.replace(destination)
    manifest = {'schema': 2, 'source': str(folder), 'file': str(destination), 'sha256': digest.hexdigest(), 'bytes': expected,
                'width': w, 'height': h, 'frames': len(packets), 'settings_first_frame': {k: v for k, v in packets[0]['metadata'].items() if k not in ('observer', 'guide_uniforms')},
                'initialization': 'Reset every neural context at the first replayed sample; preserve history thereafter except recorded common resets. The original hidden history was not saved.',
                'frame_headers': [{'index': p['index'], 'frame_id': p['frame_id'], 'input_qpc': p['input_qpc'], 'common_reset': common_reset(p)} for p in packets]}
    destination.with_suffix(destination.suffix + '.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    return manifest


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folder', help='DLSS5-Motion-Captures/<id> folder')
    ap.add_argument('--out', help='destination .sequence file; without it the folder is only verified')
    a = ap.parse_args()
    if a.out:
        m = pack(a.folder, a.out)
        print(json.dumps({k: m[k] for k in ('file', 'frames', 'width', 'height', 'bytes', 'sha256')}, indent=2))
    else:
        s, packets, extent = verify(a.folder)
        print(json.dumps({'verified': True, 'frames': len(packets), 'extent': extent}, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
