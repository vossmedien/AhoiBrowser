# Focused receiver checks under explicit UI-access exception

Source48f7d716d15eeb763e2e1eee6a5ba53bb43d3396 differs from installed55ab only
in the existing sync_unittest.cc. No product/engine/wire/schema/key change.
Root explicitly authorized these checks while the verified locked Mac prevents
visible peer acceptance. No CUA/Phone/lock-state polling is part of this run.

Existing target ahoi_sync_unittests; prior read-only Ninja plan63 actions.
Guarded build36731 uses2jobs after fresh46%CPU idle/74GiB free. The required
wrapper also validates chrome; its test-commit-stamped app is not installed.
The installed55ab original/entitled candidates and previous rollbacks stay intact.

Only SyncReceiveBoundaryTest.* runs, one job and zero retries:

- CachedBurstPreservesOutgoingRetryAndFetchClaim: existing cache only, no upload
  or fetch, unchanged outgoing retry/initial-fetch claim, coalesced notifications.
- OriginalDeliveryLeaseStaysRevokedAfterOffOn: the original selected authority
  blocks posted import and ACK/UI even after a fresh consent is issued.
- RevocationAtCommitRollsBackReceiveTransaction: real in-memory SyncStore
  transaction rolls back if the original lease is revoked at precommit.
- RevocationAfterAckCannotPublishUiState: an original lease revoked after its
  valid ACK cannot publish UI state merely because a new lease exists.

The stores are in-memory and consent uses BrowserSettingConsent. A small extension
of the file's existing fake provider supplies cached batches only. These are
regressions at the consumer/store boundaries, not a real transport, crypto, push,
open-window or physical-peer pass. Actual peer-arrival acceptance remains open.
