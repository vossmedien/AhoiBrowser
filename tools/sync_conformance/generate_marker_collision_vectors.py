#!/usr/bin/env python3
"""Pin opaque-key marker collisions without changing frozen projection fixture 112."""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import generate_merge_vectors as merge_vectors  # noqa: E402
import generate_workspace_projection_vectors as projection  # noqa: E402

OUTPUT = projection.ROOT / "fixtures/sync-conformance/workspace_merge_marker_collision_v3.json"


def generate() -> dict:
    templates = merge_vectors.fixture_payloads()
    a = projection.initial(templates["workspace"], id=projection.A, name="Source", sort_key="A")
    b = projection.initial(templates["workspace"], id=projection.B, name="Target", sort_key="B")
    merged_a = projection.edit(a, 1, {"tombstone": True, "merged_into": projection.B},
                               projection.T1)
    marker = "!:ahoi-merge-root/" + projection.B + "/"

    def root(identity: str, workspace: str, key: str) -> dict:
        row = projection.initial(templates["tree_saved_web"], id=identity,
                                 workspace_id=workspace, sort_key=key)
        row.pop("parent_id", None)
        return row

    q = root(projection.KEEP, projection.B, "Q" + marker + "x")
    z = root(projection.Y, projection.B, "Z")
    late = root(projection.X, projection.A, "A")
    no_merge = projection.frame(
        "ordinary_only", [b], [q, z],
        [(projection.B, "live", projection.B)],
        [(projection.KEEP, projection.B, None), (projection.Y, projection.B, None)],
        [(projection.B, None, [projection.KEEP, projection.Y])])
    with_merge = projection.frame(
        "late_source", [merged_a, b], [q, z, late],
        [(projection.A, "resolved-merge", projection.B),
         (projection.B, "live", projection.B)],
        [(projection.KEEP, projection.B, None), (projection.Y, projection.B, None),
         (projection.X, projection.B, None)],
        [(projection.B, None, [projection.KEEP, projection.Y, projection.X])])
    return {
        "schemaVersion": 1, "modelVersion": 3,
        "kind": "ahoi-workspace-merge-projection",
        "contract": "ADR 0012 / crest 112, 126 marker-collision supplement",
        "arrayOrders": ["as-written", "reversed", "rotate-left"],
        "arrayPermutationMode": "cartesian-product-over-workspaces-and-nodes",
        "inputStage": "authoritative raw records before native/UI projection",
        "preservation": "raw codec bytes and field clocks stay exact across passive projection",
        "cases": [
            {"name": "ordinary_key_without_merge",
             "description": "Marker-like bytes inside an ordinary B key must not move Q behind Z.",
             "frames": [no_merge]},
            {"name": "ordinary_key_with_real_late_node",
             "description": "Q remains before Z while the genuine late X follows target roots.",
             "frames": [with_merge]},
        ],
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    rendered = merge_vectors.render(generate())
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != rendered:
            print(f"{OUTPUT.relative_to(projection.ROOT)} is stale", file=sys.stderr)
            return 1
        return 0
    OUTPUT.write_text(rendered)
    print(f"wrote {len(generate()['cases'])} cases to {OUTPUT.relative_to(projection.ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
