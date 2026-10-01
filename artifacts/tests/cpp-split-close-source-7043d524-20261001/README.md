# M154 split close: source checks

Source `7043d524` adds ordered patch 0088 and the actual browser regression
`SplitLayoutMenuBrowserTest.FocusedPaneCloseRetainsLiveHostsUntilOnePane`.
The two modified Chromium translation units and the corrected regression
source each pass pinned-Clang syntax/type/style analysis, one process at a
time. Full overlay/patch composition on the pinned M154 base also succeeds.
`receipt.json` binds source, patch, compiler, generated flags and logs.

The first test source check exposed an invalid `auto*` range declaration
over Chromium raw_ptr hosts. The explicit pointer correction passed the
focused repeat; the unchanged two production checks were not repeated.

No object, link, browser test, app install or visible journey ran here. The
Build-59 lifecycle crash is not closed until a new guarded candidate passes
the real browser regression and installed split-lifecycle journey.
