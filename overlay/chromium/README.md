# Ahoi-owned Chromium overlay

Standalone Ahoi source/resources that can live in an isolated Chromium target go
here using their final path relative to Chromium `src`. `apply-overlay.sh` copies
them into the pinned checkout before applying documented patches.

Do not copy or modify upstream files here. Upstream modifications belong in
`patches/chromium/` so the delta remains reviewable during every Stable roll.
