# Comparison showcase

This directory contains the complete static comparison page for GitHub Pages. HTML, scripts, styles, images and provenance live in this repository. No account, API key or external image host is required to view it.

The public project URL is `https://einareb.github.io/ETS2-DLSS-5-VR-Preview/` once Pages is enabled. Repository Settings → Pages → Build and deployment → Source must be **Deploy from a branch**, using **gh-pages** and **/(root)**.

The `gh-pages` branch contains this directory at its root. To publish an update, validate the site, then update that branch with exactly the contents of this directory. GitHub's managed Pages build publishes ordinary branch updates; no custom workflow with repository-write permission is used.

Run `python tools/check-comparison-site.py` from the repository root before publishing. For local use, serve this directory with a static HTTP server; opening index.html directly as a file does not allow the comparison manifest to load in some browsers.

The current Medium images are paired original/result exports from real in-game VR captures. Low, High and Ultra controls accurately say their same-input comparisons are awaiting processing. When the static replay matrix is verified, update the manifest, method labels and image hashes together; do not label those future replay images as physical headset captures.
