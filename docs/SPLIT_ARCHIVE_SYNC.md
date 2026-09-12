# Format-3 split, archive and Home integration handoff

Implemented as a source packet 12 September 2026 by Common/Mobile with the
active Desktop owner. ADR0009 and the master remain authoritative. C++ and Swift
now contain records, exact wire maps, validation/merge, durable collections and
provider dispatch. Native split/archive consumers and their runtime acceptance
remain open. The separately building Desktop candidate and Device23 do not
contain this packet; no existing artifact is retroactively accepted.

## Exact record and field decisions

| Carrier | Identity | Exact new field groups |
| --- | --- | --- |
| `splitGroup`, entity13 | Stable logical split UUID | `workspace_id`, `topology`, `ratios`, `tombstone` |
| `tabArchiveEntry`, entity14 | UUIDv5 URL namespace, `ahoi:sync:archive:v1:<page-or-split>:<subject-uuid>` | `snapshot`, `state`, `tombstone` |
| existing `treeNode`2 | Existing page UUID | additional `home_target` |
| existing `workspace`1 | Existing workspace UUID | additional `archive_policy` |
| existing `permittedSetting`8 | Existing catalog identity rule | existing `value_json` carries reviewed Home/routing/shortcut configuration |

IDs0..12 remain unchanged. Records/envelopes remain format3. Both language codecs
and the one canonical golden contain the same maps. The fixture now has35
examples/15 classes, SHA256 `67f7d3f86aa7a2c7a0a786b36248c1de640fbbca632792bf4bddfc29ed16b22d`.
Existing golden consumers bind that resource directly. Their execution remains
after a runnable successor and representative visible E2E; no mixed writer or
old-data migration is added.

`topology` is the atomic ordered array `member_ids` plus `axis` and `arrangement`.
It contains2..4 unique canonical normal TreeNode UUIDs. Axes are horizontal0 /
vertical1; arrangements linear0 / mainStart1 / mainEnd2. Two/four panes use
linear. Four-pane positions are row-major top-left, top-right, bottom-left,
bottom-right. Native fields remain resolved through the existing split model.

`ratios` atomically carries `primary` and `secondary`, strict integer millionths
in0..1000000 inclusive. Native capture normalizes a finite ratio once. Two-pane
secondary defaults to500000 for new groups, but existing unused values remain
retained. Otherwise a concurrent3-to2 membership change and secondary-divider
edit would produce an invalid merge. Four-pane primary/secondary follow the existing
native axis/cross-axis semantics. Divider edits cannot overwrite membership.
Concurrent topology edits use the existing deterministic field-clock winner;
globally tombstoned/missing pages never become fabricated live members.

`home_target` atomically carries nullable `home_url`, `home_target_kind` and
`home_local_scheme`: all three keys contain null together, or one valid portable web/localOnly
target. Folders have no Home. Newly created temporary pages have none; a dormant
Home resulting from concurrent Unsave/Home edits stays as metadata, without
showing or navigating Home on a temporary page. This keeps the independent
field groups mergeable without inventing a clock or dropping the other edit.
LocalOnly carries no original
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
Final archive tombstones are absorbing even when a peer presents a later restore
or stale live snapshot. The original deletion-field clock is retained; equal
clock/different-value remains an error. Archive plaintext is bounded to512KiB,
with at most4 pages,64KiB titles,1024-byte printable ASCII sort keys and the
existing128KiB portable target bound. Unknown nested fields are rejected.
Missing prior parents yield an explicit restore target; they are not fabricated.

The native archive scheduler excludes active/saved/pinned/Keep Loaded, media,
capture/download, forms/Before-Unload and native dialog protections. A group is
eligible only in full. Remote archive state may remain locally pending; it never
closes a protected peer page or alters its account, form, navigation or focus.

## Persistence and native API handoff

The existing C++ store now uses exact schema7 because the entity discriminator
constraint changes from0..12 to0..14. Schema6 and other incompatible files are
rejected without migration/overwrite. This is independent of wire format3.
Swift snapshots additionally require `structureRevision:1` and both explicit
split/archive collections; missing markers or collections fail instead of
becoming an empty authoritative snapshot. Normal Mobile edits preserve those
collections. Home is preserved by copy/reframing, navigation touches only the
current target, and explicit Save initializes Home while Unsave clears its local
intent. Incoming concurrent dormant Home remains preserved as described above.

`ProfileSyncService::ReadWorkspaceStructure(callback)` returns all retained
records and exact canonical payloads, observed HLC, initial-fetch status and
the original profile/provider authorization. Missing/deleted cross-record
dependencies are retained for pending Native projection, never fabricated.

`PublishWorkspaceStructureIntent(intent, callback)` accepts only entity13/14.
Native supplies a complete originally versioned intent, expected canonical
payload (null for observed absence), and the captured authorization. Existing
`StampLocalMutation` creates only explicit local field changes; retries must
reuse those exact bytes/clocks. The backend compares the expected row on its
store sequence, requires first fetch completion, validates current local page/
workspace references, retains the original authorization through transaction
commit and ACK, and rejects stale concurrent writes. An identical replay ACKs
without restamping or echo. Read/apply/ACK failure requires a fresh deliberate
native decision; it is not permission to reuse an old user action under a new
account or generation. Native listeners use the existing Service notifications
to request a new projection; no polling or second engine is added.

The native owner must persist `WorkspaceRecord.archive_policy` and
`TreeNodeRecord.home_target` through its existing DTO/store/undo/duplication and
four `tab_tree_sync_adapter` conversions before this packet's native activation.
Source-only types or an API callback do not claim scheduling, materialization,
protection, UI, install or cloud acceptance.

## Remaining product and verification boundaries

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

Only syntax/formatting, generated-project references, JSON/manifest/field-map
agreement and whitespace have been checked for this source packet. No product
compilation, simulator, native build, profile, key, portal or cloud action was run
for this packet. A separate user-unlocked physical-device DDI preflight passed;
it is not domain or app runtime evidence.
The remaining Mobile Home actions, native Peek, Reader, Markdown copy, task help
and optional private-session device-authentication lock remain binding product
work. None is implied complete by the wire or metadata preservation layer.
