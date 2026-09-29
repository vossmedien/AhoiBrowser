# Durable incoming-record wake

Source455f1bbf6190f1970d096120e36669b48082af65 is the proven26 baseline plus the
explicitly assigned native receive wake. Same isolated worktree and protected
refs/ahoi-preserved; no canonical Structure/Mobile/UX WIP was included.

Exact files under overlay/chromium/src/ahoi/browser/sync/:
sync_provider.h/.cc;
cloudkit_sync_provider_mac.h/.mm/_internal.h/_consent.mm/_persistence.mm;
sync_pump.h/.cc; sync_store.h/.cc;
profile_sync_backend.h and profile_sync_backend_provider.cc;
profile_sync_service.h/.cc.

CloudKit signals only after durable staging, coalesces pending notifications,
and binds their original generation/authority. SyncPump reads the existing cache
without another fetch/upload and serializes it with active work. Receive-only
import does not clear outgoing backoff or invent completed initial full fetch.
The original authority is checked before SQLite commit and provider ACK; failed
ACK persistence restores the in-memory inbox. UI receives a post-import/ACK
snapshot through the existing dormant-tab/native projection pipeline. Old
observer/scope generations fail closed, no empty-cache import creates a loop.

Guarded app-only run60531,3jobs, start45GiB/34%CPU idle, documented low-disk
override with32GiB hard floor. Installed26 and its raw/entitled artifacts plus
all rollbacks remain protected. No Session/Sidebar rewrite, shorter polling,
second engine, wire/schema/key/portal change or reset.

Build/sign/install are not acceptance. Required final journey is a real matching
peer adding a tab while this Mac stays open, without Mac clicks, navigation/
focus changes or a simulated peer. Coordinate that exact Native UI step directly
with Root's phone operation. No peer-arrival proof is claimed yet.
