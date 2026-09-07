# Build the Windows x64 preview

Install Visual Studio 2022 Build Tools with **Desktop development with C++** and a Windows SDK supporting D3D11/D3D12. The GUI uses the Windows .NET Framework C# compiler and has no NuGet dependencies. Python 3.10+ is needed only for packaging and auditing.

Run `build.cmd` from an **x64 Native Tools Command Prompt for VS 2022**. It builds the three add-ons, setup, launcher and CPU/file tests. It does not run ETS2, load NGX, compile game shaders on a GPU or start a graphical benchmark. `test.cmd` repeats those CPU/file tests after a build.

The source includes only the small, pinned, permissively licensed build dependencies needed by these translation units. `source-dependencies.json` records their original repositories, commit IDs and exact local file hashes. Source notices are retained in `licenses`. No NVIDIA SDK headers or static library are required. The dormant upstream Vulkan/OpenGL transport code remains in the Feeder translation unit, but this preview rejects those APIs and all desktop effect runtimes.

The candidate was built with MSVC toolset **14.44.35207** and Windows SDK **10.0.26100.0**. Compiler version, timestamps and build paths can change binary hashes; this is a repeatable source build process, not a claim of bit-identical output across toolchains. The release manifest records the toolchain and the actual source and binary hashes used for a candidate.

Run `python tools/package.py` after building and reviewing the docs. It stages an explicit payload, verifies required source dependencies, writes hashes and creates the candidate ZIP in `dist`. It never searches a game installation or copies user downloads. A package is still a candidate until its physical test is recorded; the script does not publish to GitHub.

The installer and generated source ZIP omit the large comparison photos. To work on the gallery, use the repository checkout, run `python tools/build-site.py`, then serve `docs` with a local HTTP server.

## Tests and their limits

- `preview_blend_test.cpp` exercises focus/release/press behavior, runtime generations, manual overrides and delivery acknowledgement without ReShade.
- `SetupTests.cs` uses deliberately non-executable PE headers and tiny archives. It tests input validation, path containment, read locks, rollback/cancellation and settings preservation. An optional second argument can supply the official ReShade EXE for data-only archive extraction.
- `ReleaseLauncherTests.cs` drives controls and private methods against local fixtures. It never calls the production launcher entry point, game launch, shell/shortcut or physical runtime verification paths.
- `depth_match_gpu_test.cpp` builds with `build.cmd` but runs only through `test-gpu.cmd`, because it needs a D3D11 hardware device. It checks the stereo depth matcher's exact proof, the one-frame-late identity assignment, its correction when the eyes swap, its fall-back when a resource changes or a proof fails, and the bounded depth hold, on tiny synthetic eyes.
- `tools/replay_branch.py` replays a recorded lab run through the headless OpenXR fixture with add-ons from `build\`, and `tools/sequence_metrics.py` scores the recorded bursts. Both need the lab folders described in `BRANCH-NOTES.md`; the second needs numpy.
- `tools/replay_branch.py --reshade KEY=VALUE` rewrites keys of the consumer's `[RenoDX.DLSS5]` section in the copied run's `ReShade.ini` and `ReShadeVR.ini`; `--feed-cfg key=value` does the same for `dlss5-feed.cfg`. `tools/sweep_effect.py` runs a matrix of both over the recorded cab, exterior and detail scenes and writes `effect-report.md` with the effect split into tone and detail bands, the parameter values the model actually received, and the difference against the baseline and two-pass runs. Every run is 1.6 GB while it executes and is pruned to about 0.3 GB after scoring; put `--out` on a drive with room.
- `build-replay.cmd` builds `build\replay\dlss5-feed.addon64`, the feeder with the offline replay inputs compiled in (`ETS2_REPLAY_PACKET` for a frozen frame, `ETS2_REPLAY_SEQUENCE` for a consecutive sequence). It substitutes recorded inputs for the game's own and must never be installed. `tools/make_pan_sequence.py` builds a synthetic consecutive sequence from a recorded frame, optionally with vector noise, dropped vector blocks and a brightness ramp, for stressing the temporal filter without a headset.

The setup fixture retains its tiny files and a report under `%TEMP%\ets2-preview-tests` for inspection. The launcher fixture lives under `build\tests\launcher`. Tests must not be run from an installed game preview directory. See `VALIDATION.md` for what still requires a headset and the real Windows presentation chain.

## Source boundaries

The integration source is derived from Feeder 0.13.1-beta.1 and the MIT bridge. The current changes separate stereo resources, enforce matched depth, support region selection and multiple passes, preserve native detail, stabilize the neural edit, add frame-consistent comparison/status, and prepare a local installation. Shader adaptation provenance and licenses are listed in `THIRD-PARTY-NOTICES.md`.

Keep game files, Snowymoon binaries/credentials, RenoDX consumer binaries, NVIDIA model DLLs, private configurations, raw capture packets and test profiles out of this repository. Curated comparison images live in `docs/assets/gallery`. `package.py` uses explicit source and payload lists; review the resulting archive before publishing.
