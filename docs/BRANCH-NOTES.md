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

### Installed for testing on 2026-09-07 at 21:06

Both branch add-ons from commit abc754d were installed into `E:\ETS2-VR-Preview` with `tools\install_branch_build.py`; every hash recorded in `preview.json` was re-verified afterwards (64 files, no mismatch). `dlss5-feed.cfg` there now has `stereo_eye_shift=-608`; the previous cfg, both replaced add-ons and the previous `preview.json` are in `E:\ETS2-VR-Preview\branch-backups\20260907-210614\`. A desktop shortcut "ETS2 DLSS 5 VR Preview (preview-next)" points at the same launcher; the older "ETS2 DLSS 5 VR Preview" shortcut now starts the same build, because there is one prepared preview. `--restore` puts the release add-ons back; the cfg has to be restored by hand from the backup folder if wanted.

### Headset session of 2026-09-07, 21:28 to 21:35, Medium, milestone 2 build

Einar's report: Medium is now smooth enough for gameplay; High and Ultra are still unusable; on every preset the image flashes and glitches for the first thirty seconds or so, then settles and is better than before. One consecutive burst of 22 frames was recorded from the Add-ons tab (copied with the session logs to `E:\ETS2-DLSS5-Lab\preview-next\bursts\medium-m2-20260907-213442`).

| Measurement | Baseline (release, 2026-09-07 afternoon) | This session |
| --- | --- | --- |
| Frame interval, windows without captures or menus | 29.7 ms (33.6 fps) | 27.8 ms (36.0 fps, locked to half of 72 Hz) |
| Depth proof wait on the render thread | 14.3 ms median | 0.006 ms median; 7443 frames assigned by identity, 4 synchronous, 6 held |
| Feed GPU time | 8.0 ms | 7.4 ms |
| Binocular low-band difference, aligned, world only | 3.8 (spaced captures) | 0.43 |
| Flicker, motion compensated and gated, low / high | no real baseline existed | 0.30 / 0.65 |
| Source warp error with the recorded Vort vectors | not measured | 5.4 |
| Effect in the square, total / low / high | 8.0 / 7.8 / 1.0 | 6.1 / 5.9 / 1.1 |

- The first real flicker numbers land where the stressed synthetic sequence did (0.34 / 0.62 with 1.5 px vector noise), and the source warp error says the recorded optical-flow vectors misplace the previous frame by about 5/255 of colour on average. The exact-vector pan sits at 0.15 / 0.29. Vector quality is the remaining flicker source, which is milestone 4.
- Nothing in the feeder or depth logs marks the first thirty seconds: the feature is held for 60 frames, the first frame arrives 20 s after launch, then the windows read 33.5 and 36.0 fps with no control failures, no resets and no proof fall-backs. The consumer logs one harmless missing `_C` export at start. Whether the flashing is in the neural edit or in the game image itself is undetermined; pressing Scroll Lock (blend 0 %) during those seconds separates the two, and a burst recorded right after entering the cab would show the frames.
- Two-pass High in the fixture (90 % square, 80 %): four neural slots evaluate with pass two at structure 0.25, no control failures; effect 12.6, flicker 0.27 / 0.52 on the exact-vector pan and 0.53 / 0.32 on the alternating-brightness test, both in proportion to the larger edit; aligned binocular 3.3 because a 90 % square can only shift partially (left x 203, right x 0). No defect in the second pass is visible offline. Its cost is 4 × 1800² of neural input, about 13 MP against Medium's 3 MP, so the headset falls below the half-rate lock; that is milestone 5's problem.
- **The lab reproduces the game.** The recorded burst was packed with `tools/pack_sequence.py` (`E:\ETS2-DLSS5-Lab\preview-next\sequences\real-medium-20260907-213442.sequence`) and replayed through the fixture with the same build and settings. Frame for frame, the replayed residual differs from the game's own by 0.5 to 0.7 out of 255 inside the squares, the effect magnitude matches (6.2 both), the aligned binocular difference matches (0.47 against 0.43), and flicker reads 0.42 / 0.74 against 0.30 / 0.65. The replay starts its neural history cold four frames before the burst while the game had minutes of history, which accounts for the higher flicker. Real driving inputs can now be iterated offline. Harness quirk: arming the recorder before the first delivered frame (`--temporal-after 1`) recorded nothing on a 22-frame sequence; request the burst after the first delivery instead (`--burst-at-delivered 1`).

## 2026-09-07 — Milestone 3: effect strength

Einar moved this milestone ahead of camera truth and the async residual after the headset session: Medium is playable, the effect is still too weak at one pass, and only the second pass made it "wow" at a cost High and Ultra cannot pay. Low and Medium stay at one pass, High and Ultra at two. The question for the lab was which lever buys detail rather than darkening, and whether the second pass can be approximated at one-pass cost.

**Commit:** see `git log` for "Milestone 3". Files: `src/feeder/feed_stability.h` (gains), `feed_residual_composite.h`, `feed_nr_control_policy.h` and `feed_native_observer.h` (intensity write), `feed_stereo.h`, `dlss5-feed.cpp` (configuration keys, overlay), `tools/replay_branch.py`, `tools/sweep_effect.py` (new), `tools/sequence_metrics.py`, `build.cmd`, `docs/BUILD.md`. Installer, launcher, site and third-party binaries untouched.

### What changed

- `tools/replay_branch.py --reshade KEY=VALUE` rewrites the consumer's `[RenoDX.DLSS5]` section in the copied run's `ReShade.ini` and `ReShadeVR.ini`. `tools/sweep_effect.py` runs a matrix of consumer keys and feeder keys over the three recorded static scenes (cab, exterior, detail; Medium · Natural), scores every burst with `sequence_metrics.py` (frame 3 only; `analyze()` now takes a frame list), records the parameter block the model actually received, and compares each result image with the baseline and two-pass results of the same scene. Runs are pruned to about 0.3 GB after scoring and the sweep lives on C: (`...\c-2\work\lab\preview-next\sweeps\consumer-keys-20260907\effect-report.md`, 96 rows), because the first attempt filled E: to zero bytes; my earlier lab runs on E: lost their fixture eye dumps and DLL copies for the same reason (bursts kept).
- Feeder (`dlss5-feed.cfg`, Add-ons tab, launcher untouched): `stereo_gain_high` ("Detail gain") multiplies the detail band of the neural edit, `stereo_gain_low` ("Tone gain") the tone band (both need `stereo_band_split=1`), `stereo_gain_near` ("Cab strength") the whole edit on pixels whose raw depth is above 0.5 (the cab band found in milestone 0). The gains sit at the end of the stability filter, after both histories, so changing them never compounds through the filter and does not reset the history. Defaults of 1 reproduce the milestone 2 output bit for bit (`Constants` grew from 112 to 128 bytes; validated 0..4).
- `stereo_intensity` writes `DLSSNR.Intensity` into every owned evaluation through the control policy (restored after the call like tone and structure, both eyes reset together). It is a lab probe and is not in the overlay; see below for why.
- `build.cmd` calls `test.cmd` by full path: the sandbox this branch is built in sets `NoDefaultCurrentDirectoryInExePath`, and `call test.cmd` then fails even from the repo folder.

### Numbers (headless fixture, static scenes, Medium · Natural, one pass unless stated; effect = mean |result − original| in the processed square, out of 255, split into tone band ≈ 24 px and wider / detail band)

| Setting | Cab total / tone / detail | Exterior | Detail scene |
| --- | --- | --- | --- |
| Baseline (intensity 2, structure 0.5, tone 1, style 1, preset 1) | 7.5 / 7.2 / 1.2 | 10.4 / 10.1 / 1.2 | 7.5 / 7.4 / 0.7 |
| Intensity 1, 3, 6, 8 (model received the value) | identical to baseline within 0.06 | identical | identical |
| Structure 1.0 | 9.4 / 8.8 / 1.8 | 11.4 / 10.9 / 1.5 | 8.7 / 8.6 / 1.0 |
| Structure 1.5 | 9.3 / 8.6 / 1.9 | 11.6 / 11.1 / 1.7 | 8.8 / 8.6 / 1.1 |
| Structure 2.0 | 8.7 / 8.1 / 1.8 | 11.7 / 11.1 / 1.8 | 8.8 / 8.6 / 1.2 |
| Tone 0.5 | 4.9 / 4.6 / 1.0 | 5.9 / 5.6 / 0.8 | 4.4 / 4.3 / 0.6 |
| Tone 0 | 2.3 / 2.0 / 0.9 | 1.6 / 1.3 / 0.7 | 1.4 / 1.3 / 0.6 |
| Structure 1.5 + tone 0.5 | 7.1 / 6.5 / 1.8 | 7.4 / 6.8 / 1.5 | 6.1 / 5.8 / 1.1 |
| Style 0 (default) | 2.9 / 2.8 / 0.7 | 5.2 / 5.0 / 0.8 | 2.0 / 1.9 / 0.4 |
| Style 2 (cinematic) | 4.8 / 4.5 / 0.9 | 4.6 / 4.4 / 0.9 | 4.1 / 4.0 / 0.5 |
| Presets 0, 2, 3; skin 0; NRColorStrength, NRTransferStrength, NRPaperWhiteScale, NRDepthMode | no change (≤ 0.1) | no change | no change |
| Detail gain 2 | 8.1 / 7.4 / 2.1 | 10.8 / 10.2 / 2.0 | 7.7 / 7.4 / 1.2 |
| Detail gain 3 | 8.7 / 7.5 / 3.0 | 11.3 / 10.3 / 2.9 | 7.9 / 7.5 / 1.8 |
| Tone gain 0.5 | 4.0 / 3.6 / 1.1 | 5.4 / 5.1 / 1.1 | 3.8 / 3.7 / 0.6 |
| Tone gain 2 | 14.8 / 14.4 / 1.7 | 20.4 / 20.1 / 1.5 | 14.9 / 14.8 / 0.8 |
| Both gains 2 | 15.1 / 14.4 / 2.4 | 20.7 / 20.1 / 2.3 | 15.0 / 14.8 / 1.3 |
| Cab strength 0 / 2 | 4.6 / 4.5 / 0.7 and 10.5 / 10.0 / 1.8 | unchanged (no cab pixels) | unchanged |
| Two passes (pass two at tone/structure × 0.5, the High/Ultra setting) | 11.7 / 11.3 / 2.0 | 18.3 / 17.8 / 2.0 | 13.0 / 12.9 / 1.1 |
| Two passes, pass two at full tone/structure | 12.3 / 11.8 / 2.1 | 18.4 / 17.9 / 2.0 | 13.4 / 13.2 / 1.2 |

- **Intensity is inert in this pipeline.** The consumer caps `NRIntensity` at 2, but that is not the limit: written directly into the model's parameter block, 1, 2, 3, 6 and 8 all produce the same image within 0.06/255 in all three scenes. Whatever DLSS 5 does with Intensity elsewhere, it is not part of the neural pass this preview runs; the feeder's own composite (`work_mix`, fixed at 1) is the only strength that ever acted. That is why the slider felt useless from 0 to 2. The overlay now says so instead of offering a slider.
- **Structure is the parameter that buys detail**: 0.5 → 1.5 raises the detail band by 40–60 % in every scene, with the tone band rising 15–20 % alongside. Beyond 1.5 the detail band saturates (2.0 adds a few percent) while the cab scene's tone falls back. **Tone is nearly independent of detail**: tone 0 keeps 60–75 % of the detail band while removing 80–85 % of the tone band, so darkening can be traded away without losing the texture edit. Style 1 (the preset default) is the strongest style; 0 and 2 are much weaker in all bands.
- **The second pass is mostly more tone.** Against one pass it adds 56–76 % tone and 58–66 % detail; in absolute terms +4 to +8 tone against +0.4 to +0.8 detail. The band gains reach the same magnitudes without the second pass: detail gain 3 gives 1.5× the two-pass detail with 2–4 % tone leakage, tone gain 2 exceeds the two-pass tone, and "both gains 2" exceeds two passes in every band at one-pass cost. Whole-frame difference to the two-pass image (cab / exterior / detail): the one-pass baseline sits at 2.3 / 4.3 / 3.0; tone gain 2 reaches 1.9 / 1.6 / 1.2 and both gains 2 reach 1.9 / 1.5 / 1.2. So the gains reproduce the second pass's magnitude and about half of its image; the rest is content the second pass synthesises from the already edited image, which no gain can supply. Detail gain alone does not move toward the two-pass image (2.30 against 2.34 in the cab), consistent with the second pass being mostly tone.
- Cab strength does what the depth band promised: 0 removes 38 % of the cab scene's edit (the cab pixels inside the square) and touches nothing in the two exterior scenes, which have no cab pixels.
- Filter cost is unchanged by the gains (three multiplies per pixel in the existing stabilize pass); no new pass and no new texture.

### Not verified offline

- Whether a one-pass image with detail gain 2–3 and structure 1.5 *looks* like the two-pass image is a headset question; the lab measures magnitudes and pixel differences, not appearance or comfort.
- Gains above 1 amplify whatever flicker survives the filter in that band by the same factor; the flicker sequences were not re-run with gains (the filter's histories are untouched, so the relative flicker is the same and the absolute flicker scales with the gain).
- Nothing was installed; the launcher's presets still write `NRIntensity` (inert) and structure 0.5.

### Headset test

1. Medium (one pass). In the DLSS 5 tab set Structure to 1.5. In the Add-ons tab under "Filter tone and detail separately": Detail gain 200–300 %, Tone gain 100 %. Compare with Scroll Lock (blend 0 %). Then Tone gain 50–70 % to see whether the darkening was part of the "wow" or in its way.
2. Same on High (two passes) with gains back at 100 %: this is the reference look. If Medium with gains matches it, High and Ultra can drop to one pass in a later milestone and spend the budget on resolution instead.
3. Cab strength: 0 % to see the world edit alone, 200 % to see whether the cab wants more or less than the road.
4. Record one burst with the chosen settings so the flicker with gains can be measured.
