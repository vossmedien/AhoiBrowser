# Tab-cache reload cancellation: native source correction, UNBUILT

The previous source package advanced the helper generation and removed its
committed snapshot before native Reload. Chromium runs browser before-unload
before BeginNavigation; cancellation retains the old document and old factories,
whose old generation can no longer apply any existing network rules.

The corrected helper replaces the current committed network snapshot with the
network-only view and recreates the native current-document factories before
requesting reload. The additive Content seam delegates to Chromium's existing
factory builder and the current document's registered DedicatedWorkerHosts.
It validates the registered global frame token/pointer and active/live state;
the helper traversal excludes inner WebContents. Origin/security parameters,
interception order and native cache/network ownership remain Chromium's.
No shared/service-worker owner, global cache, new document host, script replay,
permission, persistent origin setting or transient secret serialization is added.
Generations only advance, so old in-flight responses cannot regain approval.
Native before-unload/repost can still veto reload; current-document/worker
requests then use the explicitly chosen tab policy after the native update.
Existing RenderFrameHost headers/vtable are unchanged; the patch adds a small
exported seam and its GN entries rather than modifying that shared interface.

Two new Mojo regressions are WRITTEN, NOT RUN: current-document cache changes
preserve both header directions/Prefs/navigation, and off/on cannot approve an
already dispatched response. One native browser regression is WRITTEN, NOT RUN:
warm HTTP cache, a long-lived dedicated worker, native before-unload cancellation,
retained synthetic draft/document/Prefs, worker cache bypass and both headers,
then restored native warm-cache behavior. Cleanup clears only the fixture's
handler and worker. These tests do not substitute for installed visible flows.

Final ordered composition against pinned f89f3a43 PASS without checkout mutation.
Ten actual-target/pinned-toolchain syntax commands are prepared on a private
immutable overlay/native-header snapshot. No compiler ran: an earlier attempt
rejected concurrent compilation; the final fresh aggregate gate sampled27.88%
CPU idle, below its30% floor, and exited2 before invocation, releasing its own
source-check lock. Original reports and source snapshot remain unchanged.

Next: bounded serial syntax on this corrected snapshot at current capacity,
then a guarded coherent candidate and affected installed two-tab/split/counter/
CDN/reload-cancel/reset/disable/close/restore/chip flows BEFORE necessary focused
regressions. The first free existing-installed GUI window remains archive9 and
protection6 on Development71/6bf4c233. Full Master/Mobile/Sync/external/release
scope and historical scoped GREEN/RED remain; no broad feature/goal completion.
