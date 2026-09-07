"""Put branch-built add-ons or shaders into a prepared preview folder, or take them out again.

The launcher refuses to start when a prepared component's SHA-256 differs from the
value recorded in preview.json. This script copies files from this repo's build
(and shaders) folder into the prepared preview, updates those recorded hashes, and
keeps a dated backup of every replaced file plus the original preview.json under
<preview>\\branch-backups\\<timestamp>\\. `--restore` puts the newest backup back.

Nothing else in the preview is touched: no game files, no third-party binaries,
no saves. Close ETS2 and the launcher before running either direction.

  python tools/install_branch_build.py --preview E:\\ETS2-VR-Preview
  python tools/install_branch_build.py --preview E:\\ETS2-VR-Preview --files ets2-stereo-depth.addon64 dlss5-feed.addon64
  python tools/install_branch_build.py --preview E:\\ETS2-VR-Preview --restore
"""
from pathlib import Path
import argparse, datetime, hashlib, json, shutil, sys

REPO = Path(__file__).resolve().parents[1]
SOURCES = {
    'ets2-stereo-depth.addon64': REPO / 'build' / 'depth' / 'ets2-stereo-depth.addon64',
    'dlss5-feed.addon64': REPO / 'build' / 'feeder' / 'dlss5-feed.addon64',
    'ets2-monitor.addon64': REPO / 'build' / 'monitor' / 'ets2-monitor.addon64',
}
DEFAULT_FILES = ['ets2-stereo-depth.addon64']


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def source_for(name):
    if name in SOURCES:
        return SOURCES[name]
    if name.startswith('fx/'):
        return REPO / 'shaders' / name[3:]
    raise SystemExit(f'unknown component {name}; known: {", ".join(SOURCES)} and fx/<shader>')


def install(preview, files):
    manifest = preview / 'preview.json'
    data = json.loads(manifest.read_text(encoding='utf-8-sig'))
    checked = data['checked_files']
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    backup = preview / 'branch-backups' / stamp
    backup.mkdir(parents=True)
    shutil.copy2(manifest, backup / 'preview.json')
    report = {'timestamp': stamp, 'files': {}}
    for name in files:
        src = source_for(name)
        if not src.exists():
            raise SystemExit(f'missing {src}; run build.cmd first')
        if name not in checked:
            raise SystemExit(f'{name} is not a checked component of this preview; refusing to add new files')
        dst = preview / Path(name)
        if not dst.exists():
            raise SystemExit(f'{dst} does not exist in the preview')
        (backup / Path(name)).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dst, backup / Path(name))
        before = sha(dst)
        shutil.copy2(src, dst)
        after = sha(dst)
        checked[name] = after
        report['files'][name] = {'before': before, 'after': after, 'source': str(src)}
    manifest.write_text(json.dumps(data, indent=2), encoding='utf-8')
    (backup / 'branch-install.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


def restore(preview):
    backups = sorted(p for p in (preview / 'branch-backups').glob('*') if p.is_dir()) if (preview / 'branch-backups').exists() else []
    if not backups:
        raise SystemExit('no branch backup found')
    backup = backups[-1]
    report = json.loads((backup / 'branch-install.json').read_text(encoding='utf-8'))
    for name in report['files']:
        shutil.copy2(backup / Path(name), preview / Path(name))
    shutil.copy2(backup / 'preview.json', preview / 'preview.json')
    print(json.dumps({'restored_from': str(backup), 'files': list(report['files'])}, indent=2))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--preview', required=True, help='the prepared preview folder, e.g. E:\\ETS2-VR-Preview')
    ap.add_argument('--files', nargs='*', default=DEFAULT_FILES, help='components to replace (default: the depth add-on)')
    ap.add_argument('--restore', action='store_true', help='put the newest backup back instead of installing')
    a = ap.parse_args()
    preview = Path(a.preview).resolve()
    if not (preview / 'preview.json').exists():
        raise SystemExit(f'{preview} has no preview.json')
    if (preview / 'launcher.log').exists():
        pass
    if a.restore:
        restore(preview)
    else:
        install(preview, a.files)
    return 0


if __name__ == '__main__':
    sys.exit(main())
