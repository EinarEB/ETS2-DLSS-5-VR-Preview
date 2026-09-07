"""Sweep the neural settings in the replay lab and report what each buys.

For every configuration and scene this replays the recorded static frame through the
real neural pass (tools/replay_branch.py with the fixture feeder), scores the burst
with tools/sequence_metrics.py, reads back the parameter block the consumer actually
passed to the model (so a clamped or ignored key is visible), and compares the output
with the baseline run and the two-pass run of the same scene. The report separates
the edit into the low band (tone, about 24 px and wider) and the high band (detail),
because the question is which settings buy detail rather than darkening.

Consumer keys go through the run's ReShade ini ([RenoDX.DLSS5]); feeder keys go
through dlss5-feed.cfg and need the replay feeder built from this branch.

  python tools/sweep_effect.py --out E:\\ETS2-DLSS5-Lab\\preview-next\\sweeps\\effect-20260907 [--scenes cab exterior detail] [--configs name ...]

Runs take about half a minute each. Existing baseline/two-pass runs in --out are reused
as references when a subset is run.
"""
from pathlib import Path
import argparse, json, re, subprocess, sys, time
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sequence_metrics as sm  # noqa: E402

REPO = Path(__file__).resolve().parents[1]
RUNS_ROOT = Path('E:/ETS2-DLSS5-Lab/20260907/public-showcase-v02/runs')

# name, consumer ini keys, feeder cfg keys
CONFIGS = [
    ('baseline', {}, {}),
    ('intensity-1', {'NRIntensity': '1'}, {}),
    ('intensity-4-ini', {'NRIntensity': '4'}, {}),
    ('structure-1.0', {'NRLocalStructure': '1'}, {}),
    ('structure-1.5', {'NRLocalStructure': '1.5'}, {}),
    ('structure-2.0', {'NRLocalStructure': '2'}, {}),
    ('tone-0.5', {'NRLocalTone': '0.5'}, {}),
    ('tone-0', {'NRLocalTone': '0'}, {}),
    ('skin-0', {'NRSkinStructure': '0'}, {}),
    ('style-default', {'NRStyle': '0'}, {}),
    ('style-cinematic', {'NRStyle': '2'}, {}),
    ('preset-0', {'NRPreset': '0'}, {}),
    ('preset-2', {'NRPreset': '2'}, {}),
    ('preset-3', {'NRPreset': '3'}, {}),
    ('colorstrength-2', {'NRColorStrength': '2'}, {}),
    ('transferstrength-2', {'NRTransferStrength': '2'}, {}),
    ('paperwhite-2', {'NRPaperWhiteScale': '2'}, {}),
    ('depthmode-1', {'NRDepthMode': '1'}, {}),
    ('structure-1.5-tone-0.5', {'NRLocalStructure': '1.5', 'NRLocalTone': '0.5'}, {}),
    # feeder-side (this branch): intensity written into the model's parameter block, output gains, two passes
    ('intensity-3', {}, {'stereo_intensity': '3'}),
    ('intensity-4', {}, {'stereo_intensity': '4'}),
    ('intensity-6', {}, {'stereo_intensity': '6'}),
    ('intensity-8', {}, {'stereo_intensity': '8'}),
    ('gain-high-2', {}, {'stereo_gain_high': '2'}),
    ('gain-high-3', {}, {'stereo_gain_high': '3'}),
    ('gain-low-0.5', {}, {'stereo_gain_low': '0.5'}),
    ('gain-low-2', {}, {'stereo_gain_low': '2'}),
    ('gain-both-2', {}, {'stereo_gain_low': '2', 'stereo_gain_high': '2'}),
    ('gain-near-0', {}, {'stereo_gain_near': '0'}),
    ('gain-near-2', {}, {'stereo_gain_near': '2'}),
    ('two-pass', {}, {'stereo_passes': '2'}),
    ('two-pass-full-second', {}, {'stereo_passes': '2', 'stereo_second_tone': '1', 'stereo_second_structure': '1'}),
]
OBSERVED = ('DLSSNR.Intensity', 'DLSSNR.LocalStructureStrength', 'DLSSNR.LocalToneStrength', 'DLSSNR.SkinStructureStrength', 'DLSSNR.Style', 'preset')
REFERENCES = ('baseline', 'two-pass')


def source_run(scene):
    runs = sorted(RUNS_ROOT.glob(f'{scene}-medium-natural-2026*'))
    if not runs:
        raise SystemExit(f'no lab run for scene {scene}')
    return runs[-1]


def latest_run(out, scene, name):
    exact = re.compile(rf'^sweep-{re.escape(scene)}-{re.escape(name)}-\d{{8}}-\d{{6}}$')
    for run in sorted((r for r in Path(out).glob(f'sweep-{scene}-{name}-*') if exact.match(r.name)), reverse=True):
        try:
            result = json.loads((run / 'replay-result.json').read_text(encoding='utf-8'))
        except (OSError, ValueError):
            continue
        if result.get('bursts_complete'):
            # runs may have been moved since they were written; address the burst through this run folder
            result['run'] = str(run)
            result['bursts_complete'] = [str(run / 'DLSS5-Motion-Captures' / Path(b).name) for b in result['bursts_complete']]
            return result
    return None


def replay(scene, name, reshade, feed, out):
    label = f'sweep-{scene}-{name}'
    cmd = [sys.executable, str(REPO / 'tools' / 'replay_branch.py'), '--source', str(source_run(scene)), '--out', str(out), '--label', label,
           '--feeder', '--burst-at-delivered', '120', '--temporal-count', '4', '--feed-cfg', 'stereo_eye_shift=-608', '--timeout', '400']
    for k, v in reshade.items():
        cmd += ['--reshade', f'{k}={v}']
    for k, v in feed.items():
        cmd += ['--feed-cfg', f'{k}={v}']
    p = subprocess.run(cmd, capture_output=True, text=True)
    result = latest_run(out, scene, name)
    return result, None if result else (p.stdout[-800:] + p.stderr[-800:])


def observed(burst):
    m = json.loads((Path(burst) / 'frame-3.json').read_text(encoding='utf-8'))
    slots = [v for v in m['metadata'].get('observer', {}).get('actual_nr_parameters', []) if v]
    first = next((v for v in slots if v.get('eye') == 0 and v.get('pass') == 1), slots[0] if slots else {})
    return {k.replace('DLSSNR.', ''): first.get(k) for k in OBSERVED}, m


def result_plane(burst):
    m = json.loads((Path(burst) / 'frame-3.json').read_text(encoding='utf-8'))
    r = {i['kind']: i for i in m['images']}['result']
    return np.memmap(Path(burst) / r['file'], dtype=np.uint8, mode='r', shape=(r['height'], r['width'], 4))


def prune(run):
    """Drop what the report never reads again: fixture eye dumps, frames 0-2 planes and the copied model DLLs (about 1.3 GB of a 1.6 GB run)."""
    run = Path(run)
    for pattern in ('eye-*-frame-*.raw', 'nvngx_dlss*.dll', 'DLSS5-Motion-Captures/*/frame-[0-2]-*.raw'):
        for f in run.glob(pattern):
            f.unlink()


def mean_diff(a, b):
    total = 0.0
    for y in range(0, a.shape[0], 256):
        total += float(np.abs(a[y:y + 256, :, :3].astype(np.int16) - b[y:y + 256, :, :3].astype(np.int16)).sum())
    return total / (a.shape[0] * a.shape[1] * 3)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True, help='folder for the runs and the report')
    ap.add_argument('--scenes', nargs='*', default=['cab', 'exterior', 'detail'])
    ap.add_argument('--configs', nargs='*', default=None, help='subset of configuration names')
    ap.add_argument('--reuse', action='store_true', help='skip configurations that already have a complete run in --out')
    ap.add_argument('--report', default='report', help='report file stem inside --out')
    ap.add_argument('--keep', action='store_true', help='keep every file of every run (default prunes each run to about 0.3 GB after scoring)')
    a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    configs = [c for c in CONFIGS if not a.configs or c[0] in a.configs]
    rows = []
    started = time.monotonic()
    for scene in a.scenes:
        for name, reshade, feed in configs:
            result = latest_run(out, scene, name) if a.reuse else None
            error = None
            if not result:
                result, error = replay(scene, name, reshade, feed, out)
            row = {'scene': scene, 'config': name, 'reshade': reshade, 'feed': feed}
            if not result:
                row['error'] = error or 'no burst'
                rows.append(row); print(json.dumps(row)[:300], flush=True); continue
            burst = result['bursts_complete'][-1]
            obs, m = observed(burst)
            summary = sm.analyze(burst, stride=2, cab_depth=0.5, frames=[3] if not (Path(burst) / 'frame-0-result.raw').exists() else None)['summary']
            e, b = summary['eyes'], summary['binocular']
            row.update(run=result['run'], burst=burst, observed=obs, passes=m['metadata'].get('passes_per_eye'),
                       effect_total=round(e['effect_square_total']['mean'], 2), effect_low=round(e['effect_square_low']['mean'], 2),
                       effect_high=round(e['effect_square_high']['mean'], 2), effect_eye=round(e['effect_eye_total']['mean'], 2),
                       binocular=round(b['binocular_low']['mean'], 2) if b.get('binocular_low') else None)
            rows.append(row)
            if not a.keep:
                prune(result['run'])
            print(json.dumps({k: row[k] for k in ('scene', 'config', 'observed', 'passes', 'effect_total', 'effect_low', 'effect_high') if k in row}), flush=True)
        # differences against the references, computed once per scene from whatever runs exist
        refs = {}
        for ref in REFERENCES:
            r = latest_run(out, scene, ref)
            if r:
                refs[ref] = result_plane(r['bursts_complete'][-1])
        for row in rows:
            if row['scene'] != scene or 'burst' not in row:
                continue
            plane = result_plane(row['burst'])
            for ref, rp in refs.items():
                row[f'diff_vs_{ref.replace("-", "_")}'] = round(mean_diff(plane, rp), 3)
    report = {'schema': 2, 'seconds': round(time.monotonic() - started),
              'note': 'static frame replays; effect values are mean |result - original| in the processed square, out of 255; low = about 24 px and wider, high = the rest; diff columns are mean |result - reference result| over the whole frame',
              'rows': rows}
    (out / f'{a.report}.json').write_text(json.dumps(report, indent=1), encoding='utf-8')
    lines = ['| scene | config | observed intensity / structure / tone / style / preset | passes | effect total | low | high | high share | vs baseline | vs two-pass | binocular |',
             '| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |']
    for r in rows:
        if 'error' in r:
            lines.append(f"| {r['scene']} | {r['config']} | run failed | | | | | | | | |"); continue
        o = r['observed']
        share = f"{100 * r['effect_high'] / max(r['effect_total'], 1e-6):.0f} %"
        lines.append(f"| {r['scene']} | {r['config']} | {o.get('Intensity')} / {o.get('LocalStructureStrength')} / {o.get('LocalToneStrength')} / {o.get('Style')} / {o.get('preset')} | {r.get('passes')} | {r['effect_total']} | {r['effect_low']} | {r['effect_high']} | {share} | {r.get('diff_vs_baseline', '')} | {r.get('diff_vs_two_pass', '')} | {r['binocular']} |")
    (out / f'{a.report}.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines))
    return 0


if __name__ == '__main__':
    sys.exit(main())
