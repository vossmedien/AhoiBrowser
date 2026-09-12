# Format-3 split, archive and Home integration handoff

Prepared 12 September 2026 by Common/Mobile with the active Desktop owner.
ADR0009 and the master remain authoritative. This is the concrete next source
packet; current format maps/codec/store/consumers do not activate these types.
Common `3c4b2dc` plus Native hook `4ee694e` remains the prior candidate boundary.

## Exact record and field decisions

| Carrier | Identity | Exact new field groups |
| --- | --- | --- |
| `splitGroup`, reserved entity13 | Stable logical split UUID | `workspace_id`, `topology`, `ratios`, `tombstone` |
| `tabArchiveEntry`, reserved entity14 | UUIDv5 URL namespace, `ahoi:sync:archive:v1:<page-or-split>:<subject-uuid>` | `snapshot`, `state`, `tombstone` |
| existing `treeNode`2 | Existing page UUID | additional `home_target` |
| existing `workspace`1 | Existing workspace UUID | additional `archive_policy` |
| existing `permittedSetting`8 | Existing catalog identity rule | existing `value_json` carries reviewed Home/routing/shortcut configuration |

IDs0..12 remain unchanged. Records/envelopes remain format3. New maps, both
language codecs, validation/merge/store/provider handling and the one canonical
golden must land together before activation; no mixed writer or old-data migration.

`topology` is the atomic ordered array `member_ids` plus `axis` and `arrangement`.
It contains2..4 unique canonical normal TreeNode UUIDs. Axes are horizontal0 /
vertical1; arrangements linear0 / mainStart1 / mainEnd2. Two/four panes use
linear. Four-pane positions are row-major top-left, top-right, bottom-left,
bottom-right. Native fields remain resolved through the existing split model.

`ratios` atomically carries `primary` and `secondary`, strict integer millionths
in0..1000000 inclusive. Native capture normalizes a finite ratio once. Two-pane
secondary is canonical500000. Four-pane primary/secondary follow the existing
native axis/cross-axis semantics. Divider edits cannot overwrite membership.
Concurrent topology edits use the existing deterministic field-clock winner;
globally tombstoned/missing pages never become fabricated live members.

`home_target` atomically carries nullable `home_url`, `home_target_kind` and
`home_local_scheme`: all absent/null together, or one valid portable web/localOnly
target. Folders and temporary pages have no Home. LocalOnly carries no original
path/code; newTab is forbidden as saved Home. Home changes only on explicit save/
set-Home, or initialization from existing local one-URL saved content. Current
navigation never updates Home, and incoming Home never navigates a live page.

`archive_policy` is never0 /12h1 /24h2 /7d3 /30d4. Never is the default until
conscious user choice. Snapshot has `workspace_id`, one page or all2..4 group
pages (ID, prior parent/sort, portable title/target/Home), and optional exact
split metadata. It lives in the existing domain store. `state` atomically carries
reason automatic0/manual1, `archived_at` in existing Windows microseconds, and
`restored` Boolean. Re-archive/restore updates the same subject-derived entry;
separate explicit final deletion uses tombstone/watermark, never automatic expiry.
Missing prior parents yield an explicit restore target; they are not fabricated.

The native archive scheduler excludes active/saved/pinned/Keep Loaded, media,
capture/download, forms/Before-Unload and native dialog protections. A group is
eligible only in full. Remote archive state may remain locally pending; it never
closes a protected peer page or alters its account, form, navigation or focus.

## Remaining packet boundaries

The value types are in `shared_workspace_structure_types.h` and matching Swift
`SharedWorkspaceStructure.swift`. They are not yet record variants, wire codecs
or product acceptance. Final nested bounds/strict serialization are implemented
with the coordinated record packet, not inferred from synthesized local Codable.

Home-browser configuration must bind native Home selector plus URL atomically.
Routing uses typed encrypted rules (stable rule ID/order, enabled, exact host,
explicit subdomains, optional path, logical workspace and normal/QuickWindow
mode); no query-fragment matching, native profile IDs or session grants. Shortcuts
use the shared command catalog plus explicit modifier/key binding and reset;
exact command IDs and native collision semantics must come from that catalog.
Those settings remain excluded until the matching catalog and native consumers
are present. Mobile retains supported desktop metadata, without Split UI or
QuickWindow/extension execution, and does not author emptiness for absent UI.

Next completion evidence is a matching Desktop pair and Mobile metadata
roundtrip, preserving active local pages, followed by focused cross-language,
conflict/deletion and original-consent checks. Current Device22/23 signing or
prior bookmark/search UI does not prove these additions.
