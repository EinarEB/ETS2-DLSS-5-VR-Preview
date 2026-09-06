# Validation status — candidate 21

**Public source preview; final headset validation pending.** This file distinguishes the earlier physical test from checks of this candidate. Update it with the actual final test result; do not turn a compiled binary into a headset-tested claim.

## Retained observations from the earlier private build

- User reported visible neural changes in the headset, with Medium providing useful performance and fewer artifacts than unstable High frame delivery.
- Four retained Medium timing segments were approximately 36 application FPS on RTX 5070 Ti, driver 616.64, Quest 3 / VDXR at 72 Hz. The scenes are a limited sample.
- Matched stereo input captures contained successful neural evaluations for the expected eye/pass slots.
- The monitor image was still too narrow. The proposed projection correction was not physically confirmed.

## Candidate checks

- Comparison controller: 28 CPU checks cover release/press handling, focus and key changes, configuration reloads, manual overrides, runtime generations and frame acknowledgements.
- Installer: 42 file/CPU checks cover input identity, path isolation, file locking, archive reading, cancellation, partial rollback, incomplete markers, preservation of unrelated files and a complete synthetic installation. The official ReShade setup archive was read as data and its extracted runtime matched the expected hash; it was not executed.
- Launcher: 173 CPU checks cover actual selector events, setting preservation, active/saved quality, all four model-preset choices, both-eye readiness, stale data and multi-file rollback. Four old assertions depended on blank-line layout; revised checks explicitly preserve comments and setting meaning instead.
- Candidate Feeder and Windows GUI compile without initializing NGX or starting the game.
- A real installation using the pinned local downloads completed in approximately 2.7 seconds after file selection. All six required components matched; the installed launcher's read-only verification passed. The regular game's configuration hash was unchanged. This measures file preparation, not human setup time, first launch or Snowymoon activation.
- Read-only lifetime review found and corrected a configuration reload that could bypass the supported stereo contract for one frame, and an overlay button that misleadingly offered to restart a terminal driver session.
- Final teardown review removed GPU waits and automatic COM-owner destruction from process-termination DLL callbacks. All three affected add-ons rebuilt successfully; exit behavior still needs the physical test below.
- The driver connection and monitor threading changes are **not covered by the older headset run**. Earlier driver-prototype tests establish only limited feasibility.

The check counts identify retained runs. Later source edits require the affected checks to be rerun and recorded in the release manifest; do not assume this document alone proves they passed on a different build.

[CPU check receipt](CPU-CHECKS.json) records the final run's counts, individual installer/launcher checks and tested source hashes. It records no headset test or image-quality approval.

## Final physical test required

1. Start the prepared candidate through its launcher. Confirm the exact candidate/version and selected Medium values, Snowymoon activation, both-eye neural status and no unexpected campaign changes.
2. In a driving scene, compare 0% and 100% with Scroll Lock. Check that both eyes change together, text remains readable and the selected/delivered status follows the change. Check a second press, alt-tab away/back and manual blend adjustment.
3. Look around the cab and truck with steady and moving head positions. Assess flicker, trails, eye mismatch, crop edge and frame delivery. Record inside/outside comparisons if useful.
4. Check the monitor's actual proportions, including the menu. Save a Windows screenshot if still narrow; ReShade's saved image is earlier in the presentation chain.
5. Check the VR color selector. Save a different quality setting and confirm the menu clearly says restart; return to Medium for the normal run.
6. Exit normally, let the launcher finish its settings check, then relaunch. The driver session must initialize again cleanly in a new process.
7. Verify the release download can be installed from its own documented files and steps. Dependency acquisition and subscriber activation need a real user, not fabricated credentials or packaged private state.

Any initialization failure, crash on exit, persistent one-eye error, missing download route or incorrect installer behavior blocks calling the package ready for a public quick start.
