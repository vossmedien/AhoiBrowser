# Native first-sign-in correction for c8 baseline

Observed on the real installedc8d9161 MacA after its first visible Sync ON:
the recovery area was inaccessible because of a separate layout fault. A
strictly read-only lookup of the explicitly scoped MacA inbox at12:34:53UTC
found `accountTransitionPending=true`, `zoneRecoveryPending=false`,
`bookmarkConsentRevoked=true`, generation0. The236-byte inbox was modified at
12:25:32UTC; opaque engine state was absent. A read-only retry-state query hit
`database is locked` and was not repeated or repaired. No identity, key,
token or payload values were printed. Root then visibly disabled Sync and
normally quitc8. The scope now contains setup/recovery history, not a fresh run.

Exactc8 source routed every CKSyncEngine AccountChange to ResetAccountState,
including a first/same-account SignIn. Its Swift counterpart already distinguishes
that event. This is a concrete source defect matching the runtime flags; c8 did
not persist the event subtype, so the exact subtype from that run is not claimed
as independently captured.

The six-file correction carries the account record name only in memory from
the existing bootstrap's matching before/after identity reads. A SignIn is
accepted as the same binding only with that exact current identity, absent
previous identity, valid original profile/key authorizations and no existing
account-recovery or invalid-state flag. No field is sourced from Info.plist,
logged, persisted or sent on the wire. Matching events retain pending record
bodies, mutation IDs and original per-record leases, restoring CKSyncEngine's
reset pending queue without new clocks or authorization generations.

SignOut, SwitchAccounts, unknown/mismatching identities and revoked original
authority still use the fail-closed reset path. Existing persisted recovery
flags are NOT auto-cleared: Root must use a visible, deliberate recovery action
on the corrected UI if continuing that existing profile. No reset/deletion or
automatic confirmation is part of this patch.

`first-signin.patch` is byte-bound to the source delta, SHA256
`2ed2ecac6233450bf85e3850c651ef3b1c0755a55e3a638ba1e197cefa3965ee`.
It applies cleanly to exactlyc8d9161057cae20913ddd83bfbfadf22598d3950 in an
isolated six-file snapshot; no Structure/Privacy/schema change is included.
The initial sandbox command used the wrong Git cwd and was corrected before
the successful applicability check. Formatting/whitespace and pinned SDK API
checks passed. No build or tests were run by Common for this handoff.

One event regression is added to the existing CloudKitBookmarkSyncConsentTest:
same verified SignIn preserves original queued authority, a different account
revokes it, and a subsequent matching replay cannot clear recovery. It remains
NOT_RUN until the corrected visible journey, then the focused event test.
Existing real sign-out/switch revocation behavior is unchanged. Device24 stays
Sync OFF; no Common-owned runtime, key, cloud or phone action occurred here.
