# Branch notes: preview-next

One dated entry per milestone: the numbers, what changed, what could not be verified offline, and the commit. Nothing here claims headset image quality or comfort; each entry says what the offline metric shows and what to look for in the headset.

Measurement tool: `tools/sequence_metrics.py` (needs numpy). Baseline JSON and console output live in `E:\ETS2-DLSS5-Lab\preview-next\baselines\` on the development machine; they are not part of the repository.

## 2026-09-07 — Milestone 0: harness and baselines (no renderer change)

**Commit:** see `git log` for "Milestone 0"; the renderer, shaders, installer and launcher are untouched.

### What the harness measures

All values are 8-bit code values (out of 255) unless stated.

- **Effect**: mean |result − original| inside the processed square per eye, split into a low band (8×8 box average, then a 3×3 box at 1/8 resolution, about 24 px of support at native resolution) and the remaining high band.
- **Flicker** (consecutive bursts only): mean absolute change of the residual after warping the previous frame's residual with the current frame's recorded motion vectors, per band. Pixels are excluded when the warp leaves the eye, when the recorded mask distrusts the vector, or when depth disagrees by more than 2 % (the stability filter's own gate). Frames flagged as a common neural reset are left out of the summary. This is the motion-compensated definition; a plain per-pixel temporal variance would mostly measure scene motion.
- **Binocular**: mean |left − right| of the low-band residual at 1/8 resolution after aligning the two eyes for far content (see finding 2), excluding cab-depth cells in either eye and cells outside both processed squares. The unaligned number and the fraction of each square without a processed counterpart in the other eye are reported beside it.
- **Cost**: frame interval, feed GPU and CPU time per 600-frame window from `dlss5-feed.log`; `comparison_ms` from `depth-match.log`; stage times from `feeder-profile.jsonl` when the game was started with `ETS2_FEED_PROFILE=1`.

### Two structural findings from the recorded frames

1. **ETS2 draws the cabin in its own depth band.** Raw reversed-Z depth in the recorded frames is bimodal: the cab occupies 0.90..0.94, the world sits below 0.05 and sky is exactly 0. A raw threshold of 0.5 separates cab from world exactly, with no metric calibration. This is the depth prior the plan's cab hypothesis needs, and it also means any depth linearisation that assumes one projection is wrong across the cab boundary.
2. **The two eye buffers are asymmetric frusta, and the processed squares do not cover the same world region.** Correlating the high-passed world luminance of the two eyes gives a best horizontal offset of about −608 px at 2504 px per eye (correlation 0.83 to 0.87 after the shift, about 0 without it), the same to within a few pixels on three different scenes. Far content therefore lands roughly 24 % of the eye width further left in the right eye. Because the centred square is placed at the same eye-local position in both eyes, about a third of each square has no processed counterpart in the other eye: one eye shows the neural tone edit there and the other shows the untouched original. The offset is constant for a fixed headset and runtime and can be read from the game's per-eye projection once milestone 4 recovers it, or measured from the images as the tool does.

### Medium baseline, headset session of 2026-09-07 (release build v0.1, Medium · Natural · intensity 2)

Cost from the session logs (22 windows of 600 frames while driving; the long stalls in the log were capture bursts, not gameplay):

| Measurement | Median | Range |
| --- | --- | --- |
| Frame interval | 29.9 ms | 27.7 to 33.8 ms |
| Frame interval, windows without captures | 29.7 ms | 27.7 to 30.7 ms |
| Feed GPU time (two 1216² neural passes plus copies) | 8.0 ms | 7.9 to 8.2 ms |
| Feed CPU time on the render thread | 1.1 ms | 1.1 to 4.2 ms |
| Depth proof blocking wait on the render thread (`comparison_ms`, n = 116) | 14.3 ms | 9.6 to 16.9 ms (p5 to p95) |

Effect and binocular scores from the eleven four-frame comparison captures of that session (44 real driving frames, not consecutive, so no flicker score):

| Score | Mean over captures | Range |
| --- | --- | --- |
| Effect in the square, total | 8.0 | 5.6 to 10.2 |
| Effect in the square, low band | 7.8 | 5.4 to 9.9 |
| Effect in the square, high band | 1.0 | 0.7 to 1.3 |
| Binocular low-band \|L − R\|, aligned, world only | 3.8 | 2.9 to 4.8 |
| Same, relative to the low-band magnitude | 0.40 | 0.21 to 0.57 |
| Square fraction without a processed counterpart in the other eye | 0.33 | 0.33 to 0.35 |
| Binocular low-band \|L − R\| without alignment (for reference only) | 8.6 | 4.6 to 13.2 |

The measured eye offset was −608 to −656 px (−76 to −82 cells at 1/8) with correlation 0.75 to 0.88 on open scenes and about 0.35 on the depot scene, where nearer content makes a single global shift less exact. About 40 % of the low-band edit therefore differs between the eyes even after alignment and with the cab excluded; the unaligned number is dominated by the frustum offset and is not a measure of the neural edit.

Flicker floor from the offline replay lab (static packet, motion held at zero, four consecutive frames, cab scene, Natural):

| Preset | Effect total / low / high | Flicker low / high (motion compensated, gated) |
| --- | --- | --- |
| Low | 6.9 / 6.7 / 1.2 | 0.04 / 0.08 |
| Medium | 7.4 / 7.1 / 1.2 | 0.03 / 0.08 |
| High | 12.3 / 11.8 / 1.9 | 0.04 / 0.09 |

The model is close to deterministic on a frozen input, so any flicker measured on a real driving burst is motion and history driven.

### Still needed from the headset

- Consecutive driving bursts, recorded with the release build before any renderer change, so that flicker has a baseline. Recording steps are in the first report and repeated here: start the game through the launcher, drive, then from the desktop run `Set-Content E:\ETS2-VR-Preview\dlss5-temporal.request (Get-Date -Format o)` in PowerShell, or press Home in the headset, open Add-ons → DLSS 5 Feed → Record screenshots or motion → Record motion test. The feeder records up to 24 consecutive callbacks (about 0.7 s), limited to half of the free RAM (each frame is about 221 MB at this resolution), then writes the burst to `E:\ETS2-VR-Preview\DLSS5-Motion-Captures\<timestamp>`. Rendering slows during the burst; that is expected and does not measure normal performance.
- A session started with the user environment variable `ETS2_FEED_PROFILE=1` so `feeder-profile.jsonl` gives the neural list time and the handoff time separately.

## 2026-09-07 — Milestone 1: depth proof off the render thread

**Commit:** see `git log` for "Milestone 1". Files: `src/depth/depth_match.hpp`, `src/depth/ets2_depth_route.hpp`, `src/depth/depth_match_addon.cpp`, `tests/depth_match_gpu_test.cpp`, `test-gpu.cmd`, `tools/replay_branch.py`, `tools/install_branch_build.py`, `build.cmd`, `docs/BUILD.md`. Feeder, shaders, installer, launcher and site untouched.

### What changed

- The exact colour compare still runs on every VR frame. Its 64-byte verdict is copied into a three-slot staging ring and read back with the do-not-wait flag on later frames instead of blocking the render thread. The frame is assembled from the eye assignment an earlier proof established for the same resources. A proof that fails or is ambiguous revokes that assignment, and the next frame runs the original blocking proof; so does any frame whose candidate resources differ from the proven set. The exact-match guarantee therefore holds within one frame of latency.
- The identity of a candidate is the HDR scene target the depth was snapshotted from, which the route already tracks. The final colour target is an OpenXR swapchain image that rotates every frame and cannot serve as an identity (the first build used it and never left the synchronous path).
- `ets2-stereo-depth.cfg` next to the add-on, written with defaults when missing and re-read every 120 VR frames, so it can be changed while driving: `async_proof=1` (0 restores the blocking proof on every frame) and `hold_frames=2` (0 restores the old behaviour). With a hold, a route failure publishes the last verified depth for up to that many frames instead of unpublishing it, so the feeder does not reset both neural histories for a one-frame dropout.
- `depth-match.log` MATCH lines now carry `window_mean_ms`, `window_max_ms` and `window_n` for every Match call since the previous periodic line, plus `sync`, `proof_age` and running totals of synchronous, asynchronous and held frames. `comparison_ms` is still the single-frame value and now measures the non-blocking path.
- `tests/depth_match_gpu_test.cpp` (run with `test-gpu.cmd`) checks, on tiny synthetic eyes: the blocking proof; identity assignment one frame later; an eye swap inside the same resources being accepted once and corrected the next frame without blocking; a changed byte being accepted once, revoking the assignment and then rejected by the blocking proof; a new resource forcing a blocking proof; legacy mode staying synchronous; a duplicate candidate staying ambiguous; and the bounded hold.
- `tools/replay_branch.py` replays a recorded lab run through the headless fixture with add-ons from `build\`. `tools/install_branch_build.py` copies built add-ons into a prepared preview, updates the launcher's recorded hashes in `preview.json` and keeps a dated backup; `--restore` reverts.

### Numbers (headless fixture, cab static packet, Medium · Natural, 360 frames, four-frame burst)

| Mode | Match call per frame, window mean | Window max | Frames synchronous / asynchronous |
| --- | --- | --- | --- |
| Release add-on, control run | 1.8 to 17.3 ms across the five logged samples | not logged | all synchronous |
| Branch, `async_proof=0`, two runs | 10.7 to 21.5 ms | 38 to 43 ms | 239 / 0 |
| Branch, `async_proof=1`, two runs | 0.008 to 0.013 ms | 0.03 to 0.06 ms | 1 / 238 |

- The assembled depth plane is bit-identical to the reference run in every mode.
- The neural output differs from the reference by 0.06/255 mean (maximum 5) because the burst started at a different frame; the model's own frame-to-frame floor is 0.03 to 0.05. Effect and binocular scores are unchanged (7.4 / 7.1 / 1.2 in the square; 4.8 aligned binocular).
- The hold never triggered in the fixture because the route never failed there.
- The release add-on's blocking wait in the fixture varies from 2 to 17 ms from frame to frame with the same load, so its two low samples in the original gallery run were chance, not a smaller cost.

### Not verified offline

- The frame interval in the real game. The fixture has no game load, so the 14 ms wait measured in the headset session cannot be reproduced here; the headset A/B decides how much of it comes back.
- Whether the scene-target identities stay fixed across ETS2 scene loads, weather changes and Snowymoon's extra scene passes. The totals in the MATCH lines answer this: a healthy session shows `totals_async` far above `totals_sync`.
- Whether a one-frame-late revocation is ever visible. It would appear as one frame of the other eye's depth guiding the neural pass, followed by a synchronous frame in the log.

### Headset test

1. Close ETS2 and the launcher, then `python tools\install_branch_build.py --preview E:\ETS2-VR-Preview` from the repo. It replaces only `ets2-stereo-depth.addon64`, records the new hash so the launcher accepts it, and backs up the old file and `preview.json` under `E:\ETS2-VR-Preview\branch-backups\`.
2. Start VR through the launcher as usual. Drive for a few minutes on the reference road. Then edit `E:\ETS2-VR-Preview\ets2-stereo-depth.cfg`, set `async_proof=0`, and drive the same road again; the change takes effect within about 120 frames. Optionally set it back to 1 for a third pass.
3. Read the 600-frame windows in `dlss5-feed.log` (frame interval) and the MATCH lines in `depth-match.log` (`window_mean_ms`, totals) for each pass. Baseline: 29.7 ms interval in clean windows, 14.3 ms median depth wait.
4. What to look for: nothing should change visually. Report any single-frame flash of wrong depth of field in the neural edit, and whether the occasional history reset pop after a hitch is gone.
5. `python tools\install_branch_build.py --preview E:\ETS2-VR-Preview --restore` puts the release add-on back.

## 2026-09-07 — Milestone 2: band-split stability, eye matching and aligned squares

**Commit:** see `git log` for "Milestone 2". Files: `src/feeder/feed_stability.h` (rewritten), `feed_residual_composite.h`, `feed_residual.h`, `feed_stereo.h`, `dlss5-feed.cpp` (configuration keys, overlay, capture metadata, replay input selection), `build-replay.cmd`, `tools/make_pan_sequence.py`, `tools/replay_branch.py`, `tools/sequence_metrics.py`, `tools/install_branch_build.py`, `docs/BUILD.md`. Installer, launcher, site and third-party binaries untouched.

### What changed

- **Band split.** The stability filter now runs four compute passes. The first reduces the residual to 8×8 work-pixel cells (mean residual, mean motion, nearest depth, and whether the cell lies inside its eye's processed square). The second filters that low band with its own history weight (`stereo_stability_low`, default 0.8), gated by the same 2 % depth agreement and by coverage, with no neighbourhood clamp. The third averages the low band with the other eye on cells where both eyes processed the same distant content (`stereo_cross_eye`, default 0.5). The fourth subtracts the low band from the residual and filters the remainder exactly as the previous filter did (`stereo_stability`, the tight clamp), then adds the filtered low band back. `stereo_band_split=0` restores the previous filter with the same arithmetic.
- **Eye offset and aligned squares.** `stereo_eye_shift` is the far-content offset between the eyes in native pixels (about −608 on the tested headset; `tools/sequence_metrics.py` prints it for any capture). It drives the cross-eye match and, with `stereo_crop_align=1` (restart), moves each eye's square by half of it so both squares cover the same distant content: at Medium the left square moves from x 205 to 402 and the right from 205 to 7 work pixels. The composite feathers each eye against its own square. The default offset is 0, which leaves cross-eye matching and alignment off until the value is set.
- **Harness.** `build-replay.cmd` builds the fixture feeder with both replay inputs compiled in; the environment selects packet or sequence. `tools/make_pan_sequence.py` turns one recorded frame into a consecutive sequence with a constant pan, and optionally vector noise in 32 px blocks, dropped and masked vector blocks, a brightness ramp or an alternating brightness flicker. `tools/replay_branch.py --sequence` replays such a sequence and requests the burst once a given number of frames were delivered.

### Numbers (headless fixture, cab scene, Medium · Natural, values out of 255)

| Test | Previous filter | Band split | Band split, eye offset −608 |
| --- | --- | --- | --- |
| Static frame, effect total / low / high | 7.43 / 7.13 / 1.21 | 7.41 / 7.10 / 1.21 | 7.54 / 7.22 / 1.25 |
| Static frame, binocular low-band difference, aligned (relative) | 4.78 (0.38) | 4.77 (0.38) | 0.47 (0.04) |
| Static frame, square fraction without a counterpart in the other eye | 0.33 | 0.33 | 0.00 |
| Pan 6 px per frame with exact vectors, flicker low / high | 0.15 / 0.29 | 0.16 / 0.31 | 0.17 / 0.33 |
| Pan, binocular aligned | 4.61 | 4.65 | 0.64 |
| Stress: 1.5 px vector noise, 5 % dropped blocks, 8 % brightness ramp, flicker low / high | 0.35 / 0.63 | 0.34 / 0.62 | 0.34 / 0.63 |
| Stress, binocular aligned | 4.63 | 4.65 | 0.73 |
| Alternating brightness ±3 % on a static frame, flicker low / high | 1.50 / 0.35 | 0.36 / 0.27 | not run |
| Alternating brightness, effect total | 7.31 | 6.96 | not run |
| Filter GPU time per frame, median (p95) | 0.28 (0.29) ms | 0.41 (0.42) ms | 0.41 ms |

- With exact vectors the model itself is temporally stable (0.15 / 0.29 against a 0.03 / 0.08 floor on a frozen frame), so no filter setting changes that case. Real-game flicker therefore comes from vector error, lighting change and history resets, which is what milestone 4 addresses.
- The stress case adds vector noise, which lands in the high band, and a linear brightness ramp, which an exponential average follows at the same slope; neither is a mode this filter can reduce, and the numbers say so. The alternating-brightness case isolates the mode the review predicted the old clamp passes: a region-wide tone step. There the low band improves four-fold and the high band also improves, at a 5 % cost in effect magnitude from averaging an alternating signal.
- Aligning the squares removes the third of each square that had no processed counterpart, and the cross-eye match brings the low-band eye difference from 4.8 to 0.5, about a twentieth of the edit, without reducing the edit.
- The filter costs 0.13 ms more than before at Medium.

### Not verified offline

- Any of this on real driving bursts; the synthetic sequences have no parallax, no moving objects and no depth-route dropouts.
- Whether the cross-eye match shows a seam where cab meets world, or at the square edges, in the headset.
- Whether the stereo-centred squares feel right. The processed region now sits about 200 work pixels further right in the left eye and further left in the right eye than before, so each eye's square is no longer centred on that eye's screen.
- Tone lag. A 0.8 low-band history means a tone change settles over roughly five frames; a step into shadow may read as a short fade.
- The eye offset on other headsets and resolutions; the value is per setup and the tool prints it.

### Headset test

1. Close ETS2 and the launcher, then `python tools\install_branch_build.py --preview E:\ETS2-VR-Preview` (installs both add-ons and records their hashes; `--restore` reverts).
2. In `E:\ETS2-VR-Preview\dlss5-feed.cfg` set `stereo_eye_shift=-608`, or set "Eye offset of far content" in the Add-ons tab. Start VR. The square alignment needs this first start; the other keys are live.
3. Drive the reference road with the defaults, then flip `stereo_band_split` between 1 and 0 and `stereo_cross_eye` between 50 % and 0 % from the Add-ons tab while driving.
4. Look for: less eye-to-eye tone mismatch on distant scenery, any seam at the cab edge or square edge, a short fade when entering or leaving shade, and whether the new square position is acceptable. Report the frame interval from the 600-frame windows too; the filter should cost about 0.1 ms more.
5. Then record the four bursts listed under milestone 0 with these settings, so milestones 3 and 4 have real inputs.
