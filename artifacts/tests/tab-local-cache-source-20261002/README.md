# Tab-local cache control: coherent source package, UNBUILT

A separate closed kToggleTabCacheOff action is wired in the existing native
Toolkit bubble with German/English label and its own TAB CACHE OFF chip. The
bound native tab helper retains only an in-memory bool until its tab closes
or is reset/rebound. No Pref/Sync/cache database is added. Existing explicitly
saved origin cache settings remain intact when the tab choice is turned off.

GetDeveloperNetworkProfileForTab overlays this choice only for native network
consumers. Editors keep GetDeveloperProfileForTab, so saving unrelated site
settings cannot persist the tab bit. It refuses foreign PrefService, off-record
or unsupported contexts. Main navigation, committed document snapshots, native
factory requests and late-secret approval consume the same view. Toggle
advances existing generation and retires old snapshots; native Reload creates
a new factory, preserving Chromium before-unload/repost authority. Disable
keeps local choice inert, clears request state; re-enable runs no automatic
reload/JS/compiler. Reset and WebContents replacement drop the choice. Attested
dedicated workers use their same parent document binding; no shared/service
worker owner is invented. Named persistent origin profile flags remain distinct.

Four written regression cases cover same-origin peer/editor isolation, no
saved-profile prerequisite/foreign prefs, master disable/reset generations,
and tab replacement versus explicit persistent site cache. NOT compiled/run.
Affected installed acceptance still needs two-tab/split server counters, CDN,
reload/cancelled native before-unload, reset/disable, close/restore and chips.
No existing scoped native/cache PASS is relabelled as this new feature.

Ordered composition against pinned M154 succeeds without checkout mutation.
Official GRIT generation uses actual build defines/first IDs/startup IDs and
proves an exact byte-identical baseline header. New symbol/message ID and
German translation are bound; early stream attempts failed baseline matching
and were rejected (first IDs skipped by GRIT for streams). Seven pinned-Clang
syntax commands are prepared against an immutable private overlay snapshot,
none executed while memory pressure remains2. No source refresh, GN/Ninja
build, app, compiler test, install, profile/permission or release action.

Next: actual bounded source syntax once capacity permits, guarded coherent
candidate, affected installed visible flows BEFORE focused native regressions.
Existing archive9/six-protection GUI checks remain pending and take the first
free installed71 opportunity. Full original Master/worker/Sync/Mobile/external/
release/data/archive/rollback boundaries remain active. Receipt binds all new
source plus composition/generator/plan; no secret evidence emitted.
