# 026 – Review of the 024 M1 fix (account binding of separated Workspaces)

Status: integrated by owner in `a007aa3b` (retire only for a known equal owner)
Owner lanes: mobile, sync
Reviewed: `57b6ee1` (source reading; the commit reports
`SeparatedWorkspaceSyncTests` 18/0 on A168).

Matches the request: each record remembers the account whose listing
contained it, a listing of another account pauses ("Anderes iCloud-Konto"),
and the production lister reads `container.userRecordID()`. The M2 assumption
is documented.

Two paths still retire, and so delete logins, without a confirmed matching
account:

1. **Unknown current account.** `refreshDiscovery` uses
   `try? await lister.currentAccountIdentifier()`. If that lookup fails
   transiently while `allRecordZoneNames()` succeeds, for example right after
   an account transition, `account` is nil and a missing zone takes the old,
   account-blind `retire(.zoneRemoved)` path.
2. **Records without an owner.** Separated Workspaces created before
   `57b6ee1` have `accountIdentifier == nil`. The first successful listing
   from another account retires them.

Fix: retire on `zoneRemoved` only when both identifiers are known and equal.
Otherwise pause, or skip until a listing of a known account decides. A
tombstone in the Workspace's own zone still retires as before. Since the app
is not live yet, (2) matters only for development data, but the rule costs
nothing.

- **Unit**: `retire` is not called when `currentAccountIdentifier` throws;
  a nil-owner record is paused, not retired, by a listing of a known foreign
  account.

Implementation as a patch with tests: [056](../056-step3-mobile-retire-only-for-known-owner/HANDOFF.md).
