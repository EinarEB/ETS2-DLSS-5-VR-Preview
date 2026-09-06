# Comparison page

Edit the HTML, CSS and JavaScript in `site`. Run `python tools/build-site.py` to copy those files into `docs`; all image assets live only in `docs/assets`.

Preview with `python -m http.server --directory docs`, then run `python tools/check-comparison-site.py` before publishing. GitHub Pages serves `main` / `docs` at https://einareb.github.io/ETS2-DLSS-5-VR-Preview/.

The gallery contains two captured inputs processed through four qualities, three neural styles and three color looks. Its manifest and provenance must be updated together. These are static replays of real inputs, not recordings of moving headset gameplay. Full-size exports are lossless at native resolution; the grid uses smaller lossless previews.
