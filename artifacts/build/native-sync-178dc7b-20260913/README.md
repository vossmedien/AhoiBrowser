# Explicit user retry and preserved automatic deadline

Source178dc7b216569419892c9d13db30645c7f75c145 is55 plus eight files: SyncPump,
ProfileSyncService and ProfileSyncBackend headers/implementations, the existing
sidebar Sync button handler, and provider diagnostic stageCode. Root explicitly
released this exact internal manual/automatic handoff after the real55 run.

Automatic early checks complete callbacks without changing Attempt/Deadline or
inventing success. Only the existing visible button calls SyncNowFromUser, which
bypasses this local deadline for one normal transport attempt. A manual click
during an active attempt joins it and cannot force another send. Original
profile/setting/category/key leases, SDK/server constraints and normal actual
failure backoff remain. No ClearRetry, hidden SQLite/Prefs/CKState change,
key reset, schema/wire/GN/engine change or redesign.

The exact old state is retained in ../../e2e/native-sync-55f61ca-20260913/README.md:
outbox16/ACK0, attempt13, last01:45UTC/next02:45UTC. Numeric stageCode maps:
1 ok,2 resolved_partial,3 item_lease_revoked,4 unmatched_item,
5 persist_newer_remote,6 unmatched_mutation,7 item_failure,
8 unexpected_record_delete,9 zone_failure,10 lease_revoked,
11 empty_ack,12 send_completion,0 unknown. No sensitive values are logged.

Guarded app-only run5910:3jobs, fresh46%CPU idle,52GiB free. Documented low-disk
override retains32GiB hard floor. All predecessor originals/entitled copies and
rollbacks remain protected. Build/sign/install/runtime are separate gates;
no real ACK or roundtrip is inferred from this source correction.
