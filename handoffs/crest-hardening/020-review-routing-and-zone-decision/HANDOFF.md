# 020 – Review of cross-Profile link routing and the per-Profile zone decision (H6)

Status: integrated 3b16f31, 620e5c3 (port hint in the editor; zone and key retired after 30 days; WS-ISO-06/20 by visible test)
Owner lanes: desktop (routing), sync (zones)
Reviewed: `c48a879` (routing core, patch 0057; "not yet built") and
`f8a241f` (Sync decision in `docs/ACTIVE_SYNC_COORDINATION.md`). Source
reading only.

## Routing core (`navigation/link_routing.cc`)

Matches the recommendation of the Crest comparison and ADR 0011:

- **Hosts.** The rule host and the incoming URL are both canonicalized through
  `GURL`, so case, IDN/punycode and a trailing dot agree. A rule host with a
  port, userinfo, path or `%` is rejected. Subdomains match only at a label
  boundary ("example.com.evil.net" and "notexample.com" never match).
- **Paths and schemes.** A path prefix matches only at a segment boundary.
  Query and fragment never take part. Only http/https URLs are routed.
- **Targets and fallback.** A target in a separated Workspace opens in that
  Profile's window. Links routing cannot place fall back to Chromium.

Notes:
- A rule without a port matches every port of that host (`:8443` included).
  That is fine for normal sites, but for local development
  (`localhost:3000` vs `localhost:5173`) users may expect per-port routing.
  Either document it in the editor or allow an optional port.
- **WS-ISO-06** remains the acceptance: an external link with a rule to a
  separated Workspace opens there, with no navigation in the main Profile
  first (check that the main Profile's history has no entry).

## Zone decision (Sync)

Sound: one zone and one key per separated Workspace, opt-in per Profile, the
same catalogue, and no website data, passwords or grants.

Gap: deleting a separated Workspace only tombstones it in its zone, so its
synced records stay in iCloud indefinitely.

Fix idea: after all linked devices have seen the tombstone, or after the
tombstone retention period, delete the whole zone and retire its key, so no
data of a deleted separated Workspace remains in iCloud.

- **WS-ISO-20**: delete a separated Workspace with sync on; after retention
  the zone `AhoiBrowserSyncV3-ws-<uuid>` no longer exists and its key version
  is retired; the Companion has removed its `WKWebsiteDataStore`.
