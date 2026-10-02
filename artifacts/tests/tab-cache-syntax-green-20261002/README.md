# Corrected tab-cache/native refresh: 10 syntax checks GREEN

Pinned M154 Clang syntax-only checks pass for runtime store, runtime observer,
URLLoader throttle, factory proxy, action executor, tab-cache regressions,
Toolkit bubble, factory revocation regressions, worker browser regression and
native Content factory-refresh seam. Every exit is0, exact source/plan/compiler/
log hashes and phase capacity samples retained. No unit/browser test executed,
no binary/link/GN/app/runtime/installation/release pass is implied.

The first actual check failed: runtime_store omitted the header declaring
ClearDeveloperProfileNavigationRequest and UpdateDeveloperProfileNetworkState.
The direct header include is now present. The v2 RED and rejected-capacity
attempts remain unchanged; the corrected v3 snapshot matches the tracked source.

One syntax-only compiler ran at lowered priority nice10. These short checks
use their separate15% idle floor (1.8 free logical cores on this12-core host),
normal memory pressure and owned source-check lock. Their measured phases had
25.3–48.69% idle and normal pressure; all10 finish and the lock is released.
This does not relax any build/GUI/H3 capacity or ownership gate.

Next: freeze this corrected source and create a guarded coherent candidate when
fresh build capacity permits. Actual installed two-tab/split/cache-counter/
reload-cancel/master-disable/reset/close/restore/chip acceptance comes before
necessary focused native regressions. Existing archive9/protection6 on installed
Development71 remain first free GUI work. Full original Master/Mobile/Sync and
external/release/data boundaries remain open; no broad feature/goal completion.
