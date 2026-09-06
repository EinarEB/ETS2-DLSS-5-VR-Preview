# Validation

v0.1 is an experimental preview. Automated checks cover installation, controls and file handling; they do not establish moving-headset image quality or comfort.

- Installer: 43 CPU/file checks, including exact input identity, failure before writes, path isolation, cancellation, rollback and preservation of unrelated files.
- Requirements: 25 checks covering Windows, GPU generation, dedicated memory, driver minimum, missing NGX exports and invalid or missing OpenXR components.
- Launcher: 176 checks covering preset selection, settings preservation, comparison status, both-eye readiness, stale captures and rollback.
- The native comparison controller has 28 CPU checks. All three native add-ons and both Windows applications are compiled from the release source.
- The gallery contains 72 rendered combinations from two captured frames. The original is fixed for each scene; output uses the requested quality, neural style and color look. It is a static replay, not a performance benchmark.

Remaining limitations include flicker, distortion, the experimental desktop mirror correction, and a narrow hardware/runtime test range. Normal exit, relaunch and moving stereo quality need continued headset testing. No new headset run is claimed for the release packaging changes.

Build checks run again before the release assets are published. See the repository's release workflow for the source revision and build result.
