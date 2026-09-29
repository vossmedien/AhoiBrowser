# M153 preparation — 20 September 2026

User authorized updating now to avoid completing M152 followed immediately by
another toolchain-wide build. Docker was stopped by the user, not by this task.
Root stopped only its verified M152 Ninja43443; guarded81284 ended2 normally at
4,656/20,804. Temporary dependency patches restored to original pinned hashes.
Objects, candidates, receipts, installed55ab and all Mobile38/Sync state remain.

Official discovery:153.0.8010.53, full rollout, commit
792bf6722e73a45aa9e47c163b9901bdc17f3230. `discovery.json` SHA256:
1d3aa06ecafa668a15334991b9ae07b86433e56a66baa53e8d1305bb412b0d63.
The reviewed non-production candidate binding was promoted through the existing
CLI. Production pin/checkout HEAD remain M152 until coherent source integration.

Bounded patch hydration downloaded17,821,860 response bytes (13,366,205 decoded)
for181 missing blobs;141 were already present. It covered322 touched paths.
Separate target Git commit/tree metadata was fetched, with no working-tree switch.
The hydration report verifies unchanged checkout HEAD/index/worktree. This byte
count is not the total download requirement for the subsequent dependency update.

Initial sequential preflight:43 patches,11 apply,32 conflict. Only the first
basis-patch failure is authoritative; later conflicts include missing-predecessor
effects. In the isolated sparse rebase, three-way merge auto-resolved251 affected
files and left38 real basis conflicts. Root resolved/staged BrowserView.cc/.h and
BrowserViewTabbedLayoutImpl.cc: retain M153 MultiContents hierarchy/accessor,
ActionInvocationContext/content-overlap/hover-width APIs while retaining Ahoi
overlay, sidebar viewport and shortcut behavior. Helper owns remaining series.
This is source preparation, not compile/runtime acceptance.

Toolchain handoff95449bb pins official M153 GN/Clang24/LLD/Siso and rebases the
Rust path-space patch; V8 workaround content is unchanged but bound to M153 V8.
The SDK stays26.5/25F70 and installed Xcode26.5 remains the explicitly allowed
development toolchain. No license or global Xcode selection was changed.

Disk policy now distinguishes verified existing-checkout updates from first
checkout. User explicitly allows below64 GiB with `AHOI_ALLOW_LOW_DISK=1`;
the existing32 GiB absolute build floor remains, checked at phase boundaries.
First-checkout150/120 GiB policy remains unchanged. Real guard invocation and
focused boundary checks passed; no old profiles/keys/apps were deleted.

Next: finish ordered patch rebase, verify resulting exact tree, integrate one
coherent target and use the existing guarded fetch/hooks/build path. Do not
restart M152, weaken warning/security gates or call this a release/Sync pass.
