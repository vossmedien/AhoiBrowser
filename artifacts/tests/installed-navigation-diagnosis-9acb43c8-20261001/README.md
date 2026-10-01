# Installed navigation diagnosis, 1 October 2026

Installed development source `9acb43c81917a847c2d94303053c304b981757d3`,
Chromium `154.0.8037.93`. Two serial isolated headless diagnostics, owned local
HTTP fixtures and temporary profiles, no keyboard/mouse or user-profile action.
No build, installation, external API or release action.

First run (`navigation-headless-…193615Z`): the target URL is PaneA's URL but
the initial document is still `about:blank`. Explicit `Page.navigate` replaces
that pending loader (`ERR_ABORTED`), then receives HTTP 200 and commits PaneB,
readyState complete, across all three document samples. This proves the actual
installed bundle can load the fixture through native networking; it does not
prove the aborted startup request would never have loaded.

Second run (`navigation-startup-headless-…193840Z`): observe startup separately
before replacement. The first and all subsequent startup snapshots are already
PaneA/complete. Both initial URL and explicit PaneB navigation load correctly.
The server logs two PaneA requests (independent readiness and browser) and one
PaneB. No permanent installed network failure is demonstrated. The cause of
the earlier visible lifecycle setup failure remains unproven.

Both runner states say complete, protocol-driver exit 0, native browser exit 0
and cleanup complete. Actual process inventory afterward contains neither
runner/driver/browser nor Ahoi helpers. Receipts bind raw state, target,
protocol, server/browser logs and the driver hashes recorded during execution.
The Python driver was subsequently tightened to cancel on foreign app arrival
and record its own hash; these additions are not covered by a claimed runtime
cancellation pass. Current tool syntax checks pass.

Headless diagnostics are not visible split/CU E2E, lifecycle or full Master
acceptance. The next relevant run is the corrected visible split harness under
a fresh input/ownership check, without weakening its PaneA or lifecycle checks.
