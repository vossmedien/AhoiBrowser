# 0a10bdc6: limited infrastructure run, no import acceptance

Exact DebugLocal candidate built/signed on Inhouse. The first invocation
stopped with xcodebuild64 before tests: a reused result-bundle path was already
occupied. `resultpath-harness-test.log` preserves that failure. The corrected
invocation used `files-visible-0a10bdc6.xcresult`; it executed one test and
failed at fixture discovery after225.9s, before import.

`capacity-hold.json` records a near-saturated host and the exact owned host
controller ancestry held/resumed. In-device XCTestRunner is launched separately
by CoreSimulator and continued; the host hold does not prove a paused test.
This run cannot establish a clean product regression or successful import.
No focused runtime tests were run. Original xcresult/attachments remain on target.

`cleanup.json` verifies own device Shutdown, own E2E lock release and removal
of only the content-bound staged fixture. Foreign processes/devices were
untouched. Current acceptance and the corrected picker readiness/title
selectors are in the Mobile checkpoint.
Canonical logs omit trailing blank lines; raw target logs remain unchanged.
