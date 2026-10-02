# Active-tab cache contract: source gap

The master explicitly requires cache disabled for the active tab. Existing
DeveloperProfile.cache_disabled is part of the profile stored under one
canonical origin. Editor CollectProfile writes its checkbox into that field;
DeveloperProfileTabHelper.SaveProfile retains it in the persistent profile,
HasPersistentProfileData counts it, and GetProfile loads the same Pref-backed
origin value for a different tab. Native request adapters consume that profile.

The closed DeveloperAction catalog/executor exposes cache clear and HardReload,
but lacks a distinct tab-local cache-disable command. Current tab helpers own
temporary headers and once/reload assets; no separate tab-local cache state
exists. Static finding only: no GUI reproduction or product failure crash
inferred. Existing origin-profile cache bypass/server-counter/native passes
remain valid in their recorded scope; they do not establish two-tab separation.

The next coherent desktop source package must supply the promised current-tab
control, retain existing explicitly saved origin profiles, and keep its local
choice out of Preferences/Sync. Network resolution for main requests/document
subrequests and attested dedicated workers must consume that tab choice and
revalidate reset/disable/navigation/close. UI needs a correctly attributed
CACHE OFF state without saving it through the origin editor. Use the existing
WebContents/helper/native network chain, no second cache/store/worker scanner.

Required affected installed acceptance: two same-origin tabs and split panes
with server counters, cross-origin CDN, native reload/restore/close, reset and
Toolkit disable; no new Shared/ServiceWorker owner guessed. Then necessary
focused regression on that exact coherent candidate, not a rebuild for each
helper. Source/installed app remain unchanged by this audit. Shared/service
worker, full Devtoolkit/Sync/Mobile/release and complete Master remain OPEN.

Receipt binds actual reviewed files; no secret/user profile evidence emitted.
