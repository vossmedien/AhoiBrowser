# Bookmark shelf: programmatic fallback, not visible acceptance

Date: 2026-09-05. Installed/built source:
`3d413efb5b6f196403e92f51631c346c9c55b2e5`, Chromium `152.0.7977.65`.
Build/install authority and source receipts are in `docs/ACTIVE_DESKTOP_CHECKPOINT.md`.

The Desktop owner explicitly handed over UI access. This thread's fresh CUA
inventory failed before app access with `Sky Computer Use native pipe startup
failed`. Resetting only this thread's JS session and retrying inventory failed
identically. No app/profile interaction occurred. The UI slot was handed back.
This is a technical E2E gate, not an application verdict.

After a fresh all-project CPU check, the already-built
`ahoi_sidebar_tree_unittests` ran with:

```text
--gtest_filter=SidebarBookmarkShelfViewTest.*
--test-launcher-retry-limit=0
--test-launcher-jobs=1
```

Exec session `18842` ended with exit 0. `summary.json` contains exactly eleven
executions, all `SUCCESS`; the launcher reported about six seconds. The combined
log retains the component-build warning about duplicate `ANGLESwapCGLLayer`.
No cause or wider safety claim is inferred from that warning.

Test binary SHA-256:
`4072e793f28643cd40a1d4a4c45ebeb4b68246cd4601688ddf48ba3fab5eeeca`.
Summary SHA-256:
`fec00796a2518713367e4edb5f7b504b5e9877412e4d103aee71fc62bb2ac176`.

The new shared Bookmark sync/domain source was not integrated into this candidate.
Its tests have not run. Visible Bookmark E2E, Mobile/Chromium sync and the reported
folder-motion/rendering fixes remain separate, open acceptance requirements.
