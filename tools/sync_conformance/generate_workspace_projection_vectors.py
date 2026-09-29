#!/usr/bin/env python3
"""Shared apply fixtures with hand-derived expectations, not a third projector.

Each frame is a complete raw v3 record set after field merge. Product adapters
project it in several input-array orders, preserving authoritative wire data.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import generate_merge_vectors as merge_vectors
import merge_model

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "fixtures/sync-conformance/workspace_merge_projection_v3.json"
T0, T1, T2, T3 = (11644473700000000 + n * 1000000 for n in range(4))


def workspace_id(n):
    return f"a1000000-0000-4000-8000-{n:012x}"


def node_id(n):
    return f"a2000000-0000-4000-8000-{n:012x}"


A, B, C, D = (workspace_id(n) for n in range(1, 5))
KEEP, X, Y, FOLDER, INNER, MISSING = (node_id(n) for n in (16, 17, 18, 32, 33, 255))


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()


def raw_set_hash(workspaces, nodes):
    return hashlib.sha256(canonical({
        "workspaces": sorted(workspaces, key=lambda row: row["id"]),
        "nodes": sorted(nodes, key=lambda row: row["id"]),
    })).hexdigest()


def initial(template, **changes):
    row = copy.deepcopy(template)
    stamp = merge_model.Stamp(T0, 0, merge_vectors.MAC)
    row.update(changes)
    row["modified_at"] = str(T0)
    row["version_physical"] = str(T0)
    row["version_logical"] = 0
    row["version_device"] = merge_vectors.MAC
    row["field_versions"] = {key: stamp.to_field() for key in row["field_versions"]}
    return row


def edit(row, entity, changes, time):
    return merge_vectors.edit(row, entity, merge_vectors.contract()[entity][1],
                              {**changes, "modified_at": str(time)},
                              merge_model.Stamp(time, 0, merge_vectors.PHONE))


def frame(name, workspaces, nodes, routes, effective, orders, previous=None):
    """Expectations are explicit arguments; no route/parent/order solver here."""
    if previous:
        before = {row["id"]: row for row in previous["nodes"]}
        preserved = sorted(row["id"] for row in nodes if before.get(row["id"]) == row)
    else:
        preserved = []
    return {
        "name": name, "workspaces": copy.deepcopy(workspaces), "nodes": copy.deepcopy(nodes),
        "rawRecordSetSha256": raw_set_hash(workspaces, nodes),
        "unchangedNodeIdsFromPreviousFrame": preserved,
        "expect": {
            "workspaceRoutes": [{"workspaceId": identity, "classification": kind,
                                 **({"targetWorkspaceId": target} if target else {})}
                                for identity, kind, target in routes],
            "effectiveNodes": [{"id": identity, "workspaceId": workspace, "parentId": parent}
                               for identity, workspace, parent in effective],
            "siblingOrder": [{"workspaceId": workspace, "parentId": parent, "nodeIds": ids}
                             for workspace, parent, ids in orders],
            "unconstrainedNodeIds": sorted(row["id"] for row in nodes
                if not row["tombstone"] and row["id"] not in {item[0] for item in effective}),
            "notLiveNodeIds": sorted(row["id"] for row in nodes if row["tombstone"]),
            "preserveRaw": {"workspaceIds": sorted(row["id"] for row in workspaces),
                            "nodeIds": sorted(row["id"] for row in nodes),
                            "serializedBytes": "before-equals-after-with-same-codec",
                            "fieldVersions": "exact"},
        },
    }


def generate():
    templates = merge_vectors.fixture_payloads()

    def workspace(identity, name, key):
        return initial(templates["workspace"], id=identity, name=name, sort_key=key)

    def node(identity, workspace, key, parent=None, folder=False, deleted=False):
        row = initial(templates["tree_folder" if folder else "tree_saved_web"],
                      id=identity, workspace_id=workspace, sort_key=key, tombstone=deleted)
        row.pop("parent_id", None)
        if parent:
            row["parent_id"] = parent
        return row

    wa, wb, wc = workspace(A, "Source", "A"), workspace(B, "Target", "B"), workspace(C, "Other", "C")
    merged_a = edit(wa, 1, {"tombstone": True, "merged_into": B}, T1)
    merged_b = edit(wb, 1, {"tombstone": True, "merged_into": C}, T1)
    revived_a = edit(merged_a, 1, {"tombstone": False, "merged_into": merge_vectors.ABSENT}, T3)
    keep, x, y = node(KEEP, B, "Z"), node(X, A, "A"), node(Y, A, "B")
    routes = [(A, "resolved-merge", B), (B, "live", B)]
    direct = [(KEEP, B, None), (X, B, None)]
    direct_order = [(B, None, [KEEP, X])]
    cases = []

    def add(name, description, frames, group=None):
        cases.append({"name": name, "description": description,
                      **({"convergenceGroup": group} if group else {}), "frames": frames})

    add("single_late_root", "Old key A must append after target key Z without rewriting wire data.",
        [frame("merged", [merged_a, wb], [keep, x], routes, direct, direct_order)])
    add("late_roots_source_order", "Append source roots in their source key/ID order, not arrival order.",
        [frame("merged", [merged_a, wb], [keep, y, x], routes,
               direct + [(Y, B, None)], [(B, None, [KEEP, X, Y])])])
    tied_y = node(Y, A, "A")
    add("late_roots_equal_keys", "Equal source keys break ties by stable node ID.",
        [frame("merged", [merged_a, wb], [tied_y, keep, x], routes,
               direct + [(Y, B, None)], [(B, None, [KEEP, X, Y])])])
    folder = node(FOLDER, A, "B", folder=True)
    child_x, child_y = node(X, A, "A", FOLDER), node(Y, A, "B", FOLDER)
    add("late_subtree", "Late folder and children retain hierarchy at the target.",
        [frame("merged", [merged_a, wb], [child_y, keep, folder, child_x], routes,
               [(KEEP, B, None), (FOLDER, B, None), (X, B, FOLDER), (Y, B, FOLDER)],
               [(B, None, [KEEP, FOLDER]), (B, FOLDER, [X, Y])])])
    moved_folder = edit(folder, 2, {"workspace_id": B, "sort_key": "ZZ"}, T1)
    add("known_moved_parent", "A parent already moved to B remains the late child's parent.",
        [frame("merged", [merged_a, wb], [child_x, moved_folder, keep], routes,
               [(KEEP, B, None), (FOLDER, B, None), (X, B, FOLDER)],
               [(B, None, [KEEP, FOLDER]), (B, FOLDER, [X])])])
    inner = node(INNER, A, "B", FOLDER, folder=True)
    grandchild = node(Y, A, "A", INNER)
    add("late_nested_subtree", "Projection preserves more than one folder level.",
        [frame("merged", [merged_a, wb], [grandchild, inner, child_x, keep, folder], routes,
               [(KEEP, B, None), (FOLDER, B, None), (INNER, B, FOLDER),
                (X, B, FOLDER), (Y, B, INNER)],
               [(B, None, [KEEP, FOLDER]), (B, FOLDER, [X, INNER]), (B, INNER, [Y])])])

    for kind in ("missing", "deleted", "cross_workspace", "nonfolder"):
        parent_id = MISSING if kind == "missing" else FOLDER
        inputs, ws, classified, effective, order = [keep, node(X, A, "A", parent_id)], [merged_a, wb], list(routes), list(direct), list(direct_order)
        if kind != "missing":
            parent = node(FOLDER, C if kind == "cross_workspace" else B, "ZZ",
                          folder=kind != "nonfolder", deleted=kind == "deleted")
            inputs.append(parent)
            if kind == "cross_workspace":
                ws.append(wc)
                classified.append((C, "live", C))
                effective.append((FOLDER, C, None))
                order.append((C, None, [FOLDER]))
            elif kind == "nonfolder":
                effective.append((FOLDER, B, None))
                order = [(B, None, [KEEP, FOLDER, X])]
        add("parent_" + kind, "Resolved merge with invalid parent detaches to target root, never recovery.",
            [frame("merged", ws, inputs, classified, effective, order)])

    chain_keep = node(KEEP, C, "Z")
    add("merge_chain", "A→B→C reaches live C, retaining raw A membership.",
        [frame("chain", [merged_a, merged_b, wc], [chain_keep, x],
               [(A, "resolved-merge", C), (B, "resolved-merge", C), (C, "live", C)],
               [(KEEP, C, None), (X, C, None)], [(C, None, [KEEP, X])])])

    final = frame("all_arrived", [merged_a, wb], [keep, x], routes, direct, direct_order)
    before = frame("node_arrives_first", [wa, wb], [keep, x], [(A, "live", A), (B, "live", B)],
                   [(KEEP, B, None), (X, A, None)], [(A, None, [X]), (B, None, [KEEP])])
    final_after_node = frame("all_arrived", [merged_a, wb], [keep, x], routes, direct, direct_order, before)
    add("node_before_merge", "Node delivery preceding the merge converges without a repair echo.",
        [before, final_after_node], "late_delivery")
    before = frame("merge_arrives_first", [merged_a, wb], [keep], routes,
                   [(KEEP, B, None)], [(B, None, [KEEP])])
    add("merge_before_node", "Merge delivery preceding the node has the same final raw set and view.",
        [before, frame("all_arrived", [merged_a, wb], [keep, x], routes, direct, direct_order, before)],
        "late_delivery")
    before = frame("target_unavailable", [merged_a], [x], [(A, "missing-target", None)], [], [])
    add("target_arrives_late", "An unavailable target is not a fabricated destination; later resolve it.",
        [before, frame("all_arrived", [merged_a, wb], [keep, x], routes, direct, direct_order, before)],
        "late_delivery")
    undo = frame("undo", [revived_a, wb], [keep, x], [(A, "live", A), (B, "live", B)],
                 [(KEEP, B, None), (X, A, None)], [(A, None, [X]), (B, None, [KEEP])], final)
    add("undo_returns_passive_node", "Workspace undo restores raw A's passive membership with unchanged Page clocks.",
        [final, undo])

    target_folder = node(FOLDER, B, "ZZ", folder=True)
    # A real local move into an existing target folder, not a no-op caused by
    # interpreting the projected Workspace as the raw authoritative location.
    moved_x = edit(x, 2, {"workspace_id": B, "parent_id": FOLDER}, T2)
    initial_merge = frame("merged", [merged_a, wb], [keep, target_folder, x, y], routes,
                          direct + [(FOLDER, B, None), (Y, B, None)],
                          [(B, None, [KEEP, FOLDER, X, Y])])
    moved = frame("explicit_move", [merged_a, wb], [keep, target_folder, moved_x, y], routes,
                  [(KEEP, B, None), (FOLDER, B, None), (X, B, FOLDER), (Y, B, None)],
                  [(B, None, [KEEP, FOLDER, Y]), (B, FOLDER, [X])], initial_merge)
    both_final = frame("undo_and_move", [revived_a, wb], [keep, target_folder, moved_x, y],
                       [(A, "live", A), (B, "live", B)],
                       [(KEEP, B, None), (FOLDER, B, None), (X, B, FOLDER), (Y, A, None)],
                       [(A, None, [Y]), (B, None, [KEEP, FOLDER]), (B, FOLDER, [X])], moved)
    add("explicit_move_before_undo", "X explicitly moved into B's folder stays there; passive Y returns to A.",
        [initial_merge, moved, both_final], "undo_and_explicit_move")
    undone_first = frame("undo_first", [revived_a, wb], [keep, target_folder, x, y],
                         [(A, "live", A), (B, "live", B)],
                         [(KEEP, B, None), (FOLDER, B, None), (X, A, None), (Y, A, None)],
                         [(A, None, [X, Y]), (B, None, [KEEP, FOLDER])], initial_merge)
    other_final = frame("undo_and_move", [revived_a, wb], [keep, target_folder, moved_x, y],
                        [(A, "live", A), (B, "live", B)],
                        [(KEEP, B, None), (FOLDER, B, None), (X, B, FOLDER), (Y, A, None)],
                        [(A, None, [Y]), (B, None, [KEEP, FOLDER]), (B, FOLDER, [X])], undone_first)
    add("undo_before_explicit_move", "Reversed delivery of independent workspace/node changes converges.",
        [initial_merge, undone_first, other_final], "undo_and_explicit_move")

    no_target_a = edit(wa, 1, {"tombstone": True, "merged_into": D}, T1)
    cycle_b = edit(wb, 1, {"tombstone": True, "merged_into": A}, T1)
    deleted_a = edit(wa, 1, {"tombstone": True}, T1)
    deleted_b = edit(wb, 1, {"tombstone": True}, T1)
    unresolved = [
        ("missing_merge_target", [no_target_a, wb], [keep, x],
         [(A, "missing-target", None), (B, "live", B)], B),
        ("merge_cycle", [merged_a, cycle_b, wc], [chain_keep, x],
         [(A, "cycle", None), (B, "cycle", None), (C, "live", C)], C),
        ("ordinary_deleted_target", [merged_a, deleted_b, wc], [chain_keep, x],
         [(A, "deleted-target", None), (B, "ordinary-deletion", None), (C, "live", C)], C),
        ("ordinary_deleted_source", [deleted_a, wb], [keep, x],
         [(A, "ordinary-deletion", None), (B, "live", B)], B),
        ("missing_source_workspace", [wb], [keep, x],
         [(A, "missing-source", None), (B, "live", B)], B),
    ]
    for name, ws, ns, classified, target in unresolved:
        add(name, "Classify unresolved routing; generic platform recovery is deliberately not normalized.",
            [frame("unresolved", ws, ns, classified, [(KEEP, target, None)], [(target, None, [KEEP])])])
    dead_x = edit(x, 2, {"tombstone": True}, T1)
    add("deleted_node_not_rehomed", "Do not resurrect or present a deleted node through its workspace merge.",
        [frame("deleted", [merged_a, wb], [keep, dead_x], routes,
               [(KEEP, B, None)], [(B, None, [KEEP])])])

    return {"schemaVersion": 1, "modelVersion": 3, "kind": "ahoi-workspace-merge-projection",
            "contract": "ADR 0012 / crest 084, 108, 112",
            "arrayOrders": ["as-written", "reversed", "rotate-left"],
            "arrayPermutationMode": "cartesian-product-over-workspaces-and-nodes",
            "inputStage": "authoritative raw records after field merge, before native/UI projection",
            "ordering": "existing target roots first; appended same-source roots by raw sort_key then stable id; preserve valid subtree",
            "preservation": "capture each raw record with the same local wire codec before/after projection, repeated projection, search/passive capture and reload; bytes and field clocks must match",
            "unresolvedScope": "classify merge routing separately; no assertion of platform-specific fallback Workspace, recovery folder, generated IDs or recovery ordering",
            "cases": cases}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    data = json.dumps(generate(), indent=2, ensure_ascii=False) + "\n"
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != data:
            print("workspace projection fixture is stale", file=sys.stderr)
            return 1
        return 0
    OUTPUT.write_text(data)
    print(f"wrote {len(generate()['cases'])} projection cases to {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
