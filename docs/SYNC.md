# Sync architecture

Sync is optional, local-first, provider-abstracted, and initially backed by the
user's CloudKit private database in a custom zone. Local operation never waits
for CloudKit. Record payloads designated as encrypted values are encrypted before
transport with device-held key material; CloudKit account privacy alone is not
treated as application-layer encryption.

## Data model

Every syncable entity has a UUID, schema version, hybrid logical timestamp,
device ID, stable order key, payload hash, and tombstone state. Folder/page moves
and reorders converge without using array indexes as identity. Orphans created
by concurrent deletion/move conflicts are placed in a visible recovery folder.

## Scope

The normative allow/deny lists are `config/sync-policy.json`. Extension inventory
means identifiers and desired enabled state, never CRX contents or extension
storage. Developer assets sync only after explicit per-asset opt-in and may not
contain inline secrets; secret placeholders resolve locally from Keychain.
Device tabs may report that a page is open, but window/workspace split
membership, pane order, layout, divider ratios, and focused pane are local
session state and never CloudKit records.

## Remote commands

The iOS companion can send enumerated commands to open, focus, or close a normal
Mac tab. Commands are signed, device-approved, nonce-protected, expire after five
minutes, and are stored long enough to reject replay. Targets and counts are
validated. Incognito, shell commands, arbitrary URL schemes, bulk destructive
operations, secret transfer, and developer override execution are forbidden.

## Recovery and migration

Schema migrations are forward-tested with offline devices. Corrupt/unknown
records are quarantined, not dropped. Users can disable sync, export permitted
records, delete CloudKit data, rotate encryption material through a documented
recovery flow, and continue locally after account or service failure.
