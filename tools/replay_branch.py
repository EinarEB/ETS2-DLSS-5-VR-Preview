"""Replay a recorded lab run with binaries from this branch's build. No headset, no game.

A lab run folder (E:\\ETS2-DLSS5-Lab\\...\\runs\\<case>) already holds the headless
OpenXR fixture, ReShade, the consumer, the model DLLs, the fixed photo inputs, the
shaders and the configuration that produced a gallery image. This script copies such a
run without its outputs, drops in add-ons from `build\\` (the depth add-on by default,
optionally the feeder), writes an `ets2-stereo-depth.cfg` from --cfg pairs, and runs
the fixture again with the same arguments and environment as the source run. The
feeder records a consecutive burst once `ETS2_FEED_TEMPORAL_AFTER` callbacks passed.

Outputs land in --out/<label>-<timestamp>/ with host.log, dlss5-feed.log,
depth-match.log and DLSS5-Motion-Captures/<burst>/, ready for tools/sequence_metrics.py.
The source run is never modified. Requires only the standard library.
"""
from pathlib import Path
import argparse, datetime, hashlib, json, os, shutil, subprocess, sys, threading, time

SKIP_DIRS = {'DLSS5-Motion-Captures', 'DLSS5-Captures', 'screenshots'}
SKIP_SUFFIXES = ('.log', '.jsonl', '.request', '.ppm', '.raw', '.dmp')
SKIP_NAMES = {'native-input-observer.json', 'preview-status.json', 'functional-result.json', 'verified-result.json',
              'verified-grade.json', 'launch.json', 'ets2-stereo-depth.cfg', 'sequence.json', 'session.json', 'inputs-manifest.json'}


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def copy_run(source, target):
    source = Path(source); target = Path(target)
    target.mkdir(parents=True)
    for p in source.rglob('*'):
        rel = p.relative_to(source)
        if any(part in SKIP_DIRS for part in rel.parts):
            continue
        if p.is_dir():
            (target / rel).mkdir(parents=True, exist_ok=True)
            continue
        if p.name in SKIP_NAMES or p.suffix.lower() in SKIP_SUFFIXES:
            continue
        (target / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, target / rel)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--source', required=True, help='an existing lab run folder with launch.json')
    ap.add_argument('--build', default=str(Path(__file__).resolve().parents[1] / 'build'), help="this repo's build folder")
    ap.add_argument('--out', required=True, help='root folder for new runs')
    ap.add_argument('--label', default='branch')
    ap.add_argument('--no-depth', action='store_true', help="keep the source run's ets2-stereo-depth.addon64 (control run)")
    ap.add_argument('--feeder', action='store_true', help='also replace dlss5-feed.addon64 with build/replay (build-replay.cmd); implied by --sequence')
    ap.add_argument('--sequence', help='E2SEQ02 file to replay instead of the static packet (needs the replay feeder build)')
    ap.add_argument('--burst-at-delivered', type=int, default=0, help='request the consecutive burst once this many frames were delivered (polls native-input-observer.json); 0 uses --temporal-after callbacks')
    ap.add_argument('--shaders', action='store_true', help="also refresh fx/ from this repo's shaders folder")
    ap.add_argument('--cfg', action='append', default=[], help='key=value line for ets2-stereo-depth.cfg (repeatable)')
    ap.add_argument('--feed-cfg', action='append', default=[], help='key=value override for dlss5-feed.cfg (repeatable)')
    ap.add_argument('--frames', type=int, default=None, help='fixture frame count (default: same as the source run)')
    ap.add_argument('--temporal-after', type=int, default=180, help='feeder callbacks before the consecutive burst is requested')
    ap.add_argument('--temporal-count', type=int, default=4, help='frames in the burst (4..48, memory bound)')
    ap.add_argument('--timeout', type=int, default=300)
    a = ap.parse_args()

    source = Path(a.source).resolve()
    launch = json.loads((source / 'launch.json').read_text(encoding='utf-8'))
    build = Path(a.build).resolve()
    run = Path(a.out).resolve() / (a.label + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    copy_run(source, run)
    replaced = {}
    depth = build / 'depth' / 'ets2-stereo-depth.addon64'
    if a.no_depth:
        replaced['ets2-stereo-depth.addon64'] = 'kept from source: ' + sha(run / 'ets2-stereo-depth.addon64')
    else:
        if not depth.exists():
            raise SystemExit(f'missing {depth}; run build.cmd first')
        shutil.copy2(depth, run / 'ets2-stereo-depth.addon64'); replaced['ets2-stereo-depth.addon64'] = sha(depth)
    if a.feeder or a.sequence:
        feeder = build / 'replay' / 'dlss5-feed.addon64'
        if not feeder.exists():
            raise SystemExit(f'missing {feeder}; run build-replay.cmd first')
        shutil.copy2(feeder, run / 'dlss5-feed.addon64'); replaced['dlss5-feed.addon64'] = sha(feeder)
    if a.shaders:
        fx = Path(__file__).resolve().parents[1] / 'shaders'
        for p in fx.iterdir():
            if p.is_file():
                shutil.copy2(p, run / 'fx' / p.name)
        replaced['fx'] = 'repo shaders'
    if a.cfg:
        (run / 'ets2-stereo-depth.cfg').write_text(''.join(line.strip() + '\n' for line in a.cfg), encoding='utf-8')
    if a.feed_cfg:
        cfg = run / 'dlss5-feed.cfg'
        lines = dict(l.split('=', 1) for l in cfg.read_text(encoding='utf-8').splitlines() if '=' in l)
        for item in a.feed_cfg:
            k, v = item.split('=', 1); lines[k.strip()] = v.strip()
        cfg.write_text(''.join(f'{k}={v}\n' for k, v in lines.items()), encoding='utf-8')

    args = list(launch['arguments'])
    args[0] = str(run / Path(args[0]).name)
    if a.frames:
        args[3] = str(a.frames)
    env = os.environ.copy()
    env.update({'ETS2_ROUTE_CASE': 'Z'})
    env.update(launch.get('explicit_environment', {}))
    env['ETS2_FEED_TEMPORAL_COUNT'] = str(a.temporal_count)
    if a.sequence:
        env.pop('ETS2_REPLAY_PACKET', None)
        env['ETS2_REPLAY_SEQUENCE'] = str(Path(a.sequence).resolve())
    if a.burst_at_delivered:
        env.pop('ETS2_FEED_TEMPORAL_AFTER', None)
    else:
        env['ETS2_FEED_TEMPORAL_AFTER'] = str(a.temporal_after)
    keys = ('ETS2_ROUTE_CASE', 'ETS2_REPLAY_PACKET', 'ETS2_REPLAY_SEQUENCE', 'ETS2_FEED_TEMPORAL_COUNT', 'ETS2_FEED_TEMPORAL_AFTER')
    (run / 'launch.json').write_text(json.dumps({'arguments': args, 'explicit_environment': {k: env[k] for k in keys if k in env},
                                                  'burst_at_delivered': a.burst_at_delivered, 'source_run': str(source), 'replaced': replaced, 'cfg': a.cfg, 'feed_cfg': a.feed_cfg}, indent=2), encoding='utf-8')
    start = time.monotonic()
    stop = threading.Event()

    def monitor():
        # The feeder rewrites native-input-observer.json about once a second with the
        # latest delivered frame id; request the burst once enough frames were delivered.
        while not stop.wait(0.1):
            try:
                obs = json.loads((run / 'native-input-observer.json').read_text(encoding='utf-8'))
            except (OSError, ValueError):
                continue
            slots = [v for v in obs.get('actual_nr_parameters', []) if v]
            if slots and all(v.get('result') == 1 for v in slots) and min(v.get('frame_id', 0) for v in slots) >= a.burst_at_delivered:
                (run / 'dlss5-temporal.request').write_text(f'replay-branch-{time.time()}', encoding='utf-8')
                return
    thread = threading.Thread(target=monitor, daemon=True) if a.burst_at_delivered else None
    if thread:
        thread.start()
    try:
        with (run / 'host.log').open('w') as f:
            p = subprocess.run(args, cwd=run, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=a.timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    finally:
        stop.set()
        if thread:
            thread.join(2)
    host = (run / 'host.log').read_text(errors='replace')
    feed = (run / 'dlss5-feed.log').read_text(errors='replace') if (run / 'dlss5-feed.log').exists() else ''
    bursts = sorted((run / 'DLSS5-Motion-Captures').glob('*/sequence.json')) if (run / 'DLSS5-Motion-Captures').exists() else []
    complete = [b.parent for b in bursts if json.loads(b.read_text(encoding='utf-8')).get('complete')]
    result = {'run': str(run), 'exit': p.returncode, 'seconds': round(time.monotonic() - start, 1),
              'completed': f'completed {args[3]} frames' in host, 'crash': '### CRASH' in feed or 'DEVICE_REMOVED' in feed,
              'bursts_complete': [str(b) for b in complete], 'replaced': replaced}
    (run / 'replay-result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0 if p.returncode == 0 and result['completed'] and not result['crash'] else 1


if __name__ == '__main__':
    sys.exit(main())
