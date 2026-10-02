# Installed lifecycle setup RED, 2 October 2026

Canonical installer successfully activates source28506787/M154. Its candidate,
same-volume staged copy and installed bundle verify independently; installed
Info.plist readback matches. The installer receipt is bound in receipt.json.
Installed acceptance is a separate gate and remains RED/open here.

Actual corrected visible lifecycle journey starts with idle194, no app/compiler,
owned isolated profile and strict focus-yield enabled. Fixture readiness GET
returns HTTP200/PaneA. CLI Solo and Command Bar PaneA become target URLs but
both committed documents remain about:blank/title-empty; frames show no committed
HTTP URL. The server logs only the readiness request, no actual browser GET.
The harness correctly exits4/pass:false, "fixture document did not commit …".
Split/lifecycle assertions never execute; no close-crash pass/failure is inferred.

Raw target/frame/document/AX/browser/server/error/verdict files and exact hashes
are preserved in the named raw directory. Runner31745 exits terminal and owns
no remaining root; its resource lock is released. Fixture teardown log retains
the old watchdog finding, not treated as proof of a split close failure. No
user-profile, device/API/trading or release action occurred.

Next concrete diagnosis: visible-window startup/navigation protocol trace with
an owned ready HTTP fixture, explicit Page.navigate comparison and actual loader
events; correct the established cause without weakening the committed-document
check. Native148/editor5/UI338/baseline872/browser33 remain separate valid proof,
not installed visible or complete Master acceptance. This is the same unresolved
visible-loading condition seen on9acb43c8, now with actual server/DOM evidence.
