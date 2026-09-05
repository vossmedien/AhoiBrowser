# ADR 0007: Shared normal tabs across Desktop and Mobile

Status: user-facing behavior accepted on 2026-09-05. The wire migration and
cross-owner implementation are pending; the earlier internal-beta/relay tests
do not establish this new behavior.

## User contract

- All normal tabs form one logical workspace tab structure on Desktop, iPhone
  and iPad. A new tab must appear on another already-open client when its sync
  change arrives; no restart or visit to a separate device-tab catalogue.
- Saved/persistent tabs remain one uniform shared collection of page nodes.
  Temporary tabs participate too, with a subtle device icon and optional tint.
  Color alone must not carry the distinction.
- A small Mobile icon may identify a page created or saved on iPhone/iPad.
  It must not split saved tabs into device-specific sections.
- Incoming tabs are available immediately in the model/UI and load on explicit
  activation. Remote changes do not steal local focus, selection or scrolling.
- Cookies, credentials, website storage and private tabs stay device-local.
- Chromium bookmarks remain the separate logical collection in ADR 0006;
  workspace saved pages are not relabelled as bookmarks.

## One logical identity, separate runtime presence

Reuse `TreeNodeID` for the shared page/tab. Keep Chromium runtime handles,
Mobile runtime UUIDs and per-device Presence record UUIDs separate. CloudKit
record names are UUIDs without an entity-type namespace, so a Presence record
must never reuse its TreeNode UUID.

Add an optional `tree_node_id` to DeviceTab/RemoteTab presence. Mobile session
records persist an optional local `treeNodeID` binding. Opening a shared node
first resolves that binding; it may not create another runtime tab merely
because another sync notification arrived. Linked presence decorates the shared
row instead of producing a duplicate device-tab row. Unlinked legacy presence
may remain in a compatibility catalogue until migration is resolved.

`TreeNode.isTemporary` becomes the single shared persistence authority.
Saving/un-saving changes this flag while retaining the same TreeNode identity.
Chromium's native pinned position and Mobile's cached `isSaved` are not a second
sync authority. Closing a temporary tab deletes the logical temporary node;
unloading a saved tab removes only local presence, while explicit removal of
the saved page tombstones its shared node.

Remote applies carry an explicit origin guard. They must not republish the same
change as a new local mutation. A remote close must not broadcast an automatic
replacement blank tab or silently discard local unsaved work; use existing
native before-unload/recovery behavior, never resurrect the deleted identity.

## Empty and unassigned tabs

Explicitly created empty temporary tabs are still logical tabs. An empty URL
must be representable only for an explicitly temporary page and open each
platform's own new-tab surface. Do not export internal chrome/file/javascript
URLs as a cross-platform workaround.

Unassigned tabs need one stable shared Inbox identity, not a new random default
workspace per device. Freeze its ID/default metadata together with the wire
contract so offline first-run creation converges. Automatic startup/recovery
placeholders remain local until an explicit user tab/navigation action; they
must not create a replication loop when the last shared temporary tab closes.

## Provenance and compatibility

Use the immutable `created_at` field clock's device for origin when it is
actually present. The clock of `is_temporary` can identify the device which
saved a tab. No duplicate mutable creator identity is needed. Legacy records
without trustworthy provenance receive no guessed Mobile badge.

Current v2 requires exact field maps and lacks the two new fields. Implement
the change as a coordinated v3 contract, including shared C++/Swift golden
payloads. Read v1/v2 tree nodes as persistent and old presence as unlinked.
Do not silently add fields to v2 or misuse `pinned`/tombstones. Enable v3 only
with matching clients/capability gating; older data must not downgrade a v3
temporary node or erase its binding. Preserve existing data on unsupported
versions and expose an actionable compatibility state.

Legacy migration binds each local runtime to at most one still-unbound saved
node in stable order. Do not generally deduplicate by URL: multiple tabs of the
same URL can be intentional. Ambiguous bindings need a preserving migration,
not deletion or arbitrary reassignment.

## Ownership and acceptance

The Mobile owner owns local session bindings, Mobile projection/activation,
badges and tests. Desktop owns native TabTree/SessionBridge/UI integration and
the shared Chromium checkout/build/install lease. The bookmark owner currently
edits common C++ sync model/serializer/merge files; agree a combined field/version
freeze before either other owner modifies those shared seams. Shared Swift
schema/wire ownership also needs explicit agreement with ADR 0006.

One coherent acceptance wave must show both clients already open: create normal
temporary/empty tabs, save without changing identity, edit/move/reorder, close
temporary tabs and retain unloaded saved nodes, reconnect after offline edits,
and keep private tabs out. Verify no duplicate rows, echo-created tabs, focus
stealing or eager loading. Exercise the same flows through actual UI first;
then run targeted wire migration, identity, merge, tombstone, privacy and event
propagation tests. Mark simulations and real CloudKit/native-client evidence
separately. The previous two-Swift-peer relay pass is useful groundwork only.
