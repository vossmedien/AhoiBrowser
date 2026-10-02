# Native developer suite fixture RED, 2 October 2026

Build 68 c2943397 actually compiles, signs and verifies provenance. The whole
developer suite runs one job/no retries: **132 SUCCESS, 1 CRASH, 7 NOTRUN**,
exit 1, nativePass false. The 140 names are registered result rows, not 140
executed assertions. All 24 actual Mojo methods are SUCCESS, including the four
new temporary-header cases; this is separate partial evidence, not core GREEN.

`TemporaryHeadersRetireWithHelperEvenIfDocumentSurvives` calls real test
navigation, which correctly rejects its non-test primary RenderFrameHost before
the lifetime assertion: DCHECK `instance->IsTestRenderFrameHost()`, explicitly
requesting RenderViewHostTestEnabler. Its batch's seven later cases are NOTRUN.
The raw log, complete launcher summary and exact executable/component manifest
hashes are preserved. Test/runner processes are terminal and owner lock released.

The runtime fixture now owns `content::RenderViewHostTestEnabler` after its
BrowserTaskEnvironment, before contexts/WebContents, with the native test header.
This keeps native checks intact and fixes the test setup only. Its pinned-Clang
syntax check passes; no corrected native pass is implied before execution on a
new exact candidate. Product code and installed 9acb43c8 stay unchanged.

Next: guarded incremental candidate containing this fixture correction, actual
runtime/core and editor gates, remaining required baselines/browser gates then
canonical installation and affected visible journeys. No release/API/device
permission is added and no unchanged old suite is repeated by provider switch.
