# 024 – Review of ADR 0011 step 3 on iOS (separated Workspaces)

Status: integrated 57b6ee1 (M1 account binding with pause, M2 assumption documented, M3 already covered; WS-ISO-21/22 visible runs open, need real CloudKit)
Owner lanes: mobile, sync
Reviewed: `fd6c3b2`, `9347aae` (Companion), `e760c1f` (desktop delete fix).
Source reading only; the commits report 292 simulator unit tests green, real
CloudKit and visible UI not run.

## Matches the decision

- Zones follow the desktop naming `<base>-ws-<uuid>`, each with its own key
  account and subscription. They are never merged into the main tree.
- Pages open in `WKWebsiteDataStore(forIdentifier:)` keyed by the Workspace UUID.
- A failed zone listing changes nothing (`refreshDiscovery`).
- A missing key only pauses a separated Workspace ("Schlüssel fehlt") and
  keeps its pages and data store (`9347aae`).

## M1 (high) – A confirmed iCloud account switch deletes separated Workspaces' logins

`applyDiscoveredZoneNames` retires every known separated Workspace whose zone
is missing from a successful listing (`reason: .zoneRemoved`). Retirement
calls `removeDataStore(for:)`, which deletes that Workspace's cookies, logins
and site data. Discovery runs after `bridge.syncNow()` succeeds
(`CompanionAppModel.swift` around line 471). After the user confirms an
account transition, `syncNow()` succeeds in the **new** account, whose listing
naturally lacks the old account's zones. The result is data loss on an account
switch, contrary to the rule of `9347aae` that key or account state must never
wipe logins. The main-zone policy says the previous account's records "remain
local".

Fix idea: store the account identifier (`userRecordID().recordName`) with
each `SeparatedWorkspaceRecord`. A listing from a different account pauses
those records ("anderes iCloud-Konto") and never retires them. Retire only on
a tombstone in its own zone, or a missing zone in the same account's listing.

- **WS-ISO-21**: log in inside a separated Workspace on iOS, switch the iCloud
  account and confirm the transition. The Workspace shows as paused, and after
  switching back its login is still there.

## M2 (medium) – A zone deleted by the 30-day retention vs. an offline device

`020` retires the zone 30 days after deletion on the Mac. An iPhone that was
offline for longer then sees "zone missing" and retires correctly. An iPhone
whose local separated Workspace was never deleted anywhere, but whose zone
listing is transiently incomplete, is covered only by "listing succeeded".
CloudKit's `allRecordZones` is complete when it succeeds, so this is
acceptable. Document the assumption next to `applyDiscoveredZoneNames`.

## M3 (low) – Retirement while a page is open

Retirement with open pages sets `pendingRetirement` and retries. Check that
the pages of the retired Workspace close first (`willRemoveDataStore`) so
WebKit does not recreate the store under them.
- **WS-ISO-22**: retire (tombstone on the Mac) while a page of that Workspace
  is open on iOS. The page closes and the data store does not reappear.
