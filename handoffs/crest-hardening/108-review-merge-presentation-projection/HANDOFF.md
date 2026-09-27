# 108 – Review of 084's pure mobile presentation projection

Status: partly addressed (direction accepted; Desktop R2 source fix and regressions 61cab96; R1, retention and exact-candidate apply acceptance remain open)
Owner lanes: mobile and sync; desktop for matching apply semantics
Reviewed: ADR 0012 section 1, 084 proposal 2, SYNC field/retention rules, C++
`tab_tree_sync_adapter.cc` at `628c158`, and the owner's current uncommitted
Mobile projection. This is design/source feedback, not a frozen-code approval.

## Direction: compatible with the existing contract

ADR 0012 / WS-MERGE-06 requires the concurrently added page to appear in the
merge target on both peers. Neither that ADR nor 084 mandates rewriting a
received wire location at its existing field clock. A pure, deterministic
projection over raw records is therefore compatible, and preserves the
single-writer/field-clock rule. The mobile repository's dual wire/UI role is
a valid reason not to copy the desktop native-store implementation literally.

No parallel mobile implementation is started by Crest. Do not mark 084 done
until both clients' presentation and explicit local mutations satisfy the
same cases, with exact-candidate evidence.

## R1: root-end ordering remains a concrete gap

084 proposal 2 requires successful merge re-homing to the live target's root
end, without a recovery folder. Merely replacing the Workspace ID leaves the
old order key effective: target B has root key `Z`, late node from A has key
`A`; current Swift and C++ put the late node before `Z`, not at B's end.

The derived location/order can stay projection-only. It must be deterministic
for an identical record set, independent of arrival order or local wall time,
and must not be serialized back with the unchanged field clock. A later
explicit user move owns a real, newly stamped location write.

ADR 0012 also preserves folder/split structure. Specify the treatment of a
late subtree and a parent already moved by the merge: preserving valid
parent/child relationships and root placement must be consistent across peers.
Do not silently replace this requirement with whichever client's current
behavior is easiest to keep.

## R2: invalid-parent fallback differs between current clients

On successful target resolution, current C++ retains `parent_id`; its later
repair pass sends a node with a missing/deleted/cross-workspace parent to
`RecoveryFolderId(target)`. Current Swift clears such a parent to nil.
The C++ result violates 084's no-recovery rule for a successfully resolved
merge; the two UIs also disagree. Reconcile that case together with root-end
ordering. An unresolved/cyclic/ordinary-deletion chain remains a separate
fallback case, rather than fabricating a live destination.

## Required checks for projection/mutation consistency

These are acceptance obligations for the chosen design, not claims that the
in-progress owner code has already failed them:

- For the same raw set: source before target, target before source, node before
  tombstone, merge chains and cycles, then persist/reload. Include stable IDs,
  effective Workspace, parent and sibling order in a shared apply fixture.
- A newer undo revives A and clears its target: a passively projected raw A
  node returns to A; an explicitly moved B node with a newer location clock
  stays in B. Reordering the same deliveries must converge.
- Library/search/passive tab reconciliation, foreground recapture and
  `enqueueLocalSnapshot` must not turn the projected Page location/order into
  a wire write. Compare Page bytes/field clocks before and after; ordinary
  Presence/session heartbeat writes are not a Page-location repair.
- Create a child under a projected folder, move/delete/merge that folder,
  delete its presented target Workspace, and undo. Check both raw relationships
  and the resulting projection, with no orphaned children or accidental
  mutation of an unrelated raw Workspace. 098's later-child refusal still
  applies to the complete affected set.
- Preserve the sync-zone/profile boundary. Rehoming changes presentation or
  explicitly authorized structure; it must not transfer a live WebKit data
  store/session between isolation contexts.

## Retention dependency: additional acceptance gap, not a reproduced defect

SYNC.md specifies acknowledged tombstone compaction after 30 days and a durable
version watermark. A projection that still depends on raw `node.workspaceID=A`
needs A's merge destination after that compaction as well. A clock-only
watermark cannot reconstruct it. Verify the actual compaction/materialization
policy on both clients: the already visible node must not disappear or fall
back elsewhere when the routing tombstone is removed. This review does not
prescribe a new wire clock, a permanent tombstone or a schema change; the Sync
owner must reconcile the existing retention and convergence contracts.

104's 140 vectors test field merging, not these apply/projection cases. Keep
that evidence distinction. No tests, builds, GUI, runtime lease or shared
resource use accompanied this review.
