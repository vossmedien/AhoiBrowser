#!/usr/bin/env python3
"""Generate the inventory/asset field-merge counterexamples for H1 Format 3."""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import generate_merge_vectors as gen  # noqa: E402
import merge_model as model  # noqa: E402

OUTPUT = gen.ROOT / "fixtures/sync-conformance/merge_inventory_asset_v3.json"

SPECS = {
    9: ("extension_inventory", {"name": "Renamed extension"},
        {"name": "Alternative extension"}, {"enabled": False}),
    10: ("developer_asset_css_opt_in", {"name": "Renamed CSS asset"},
         {"name": "Alternative CSS asset"}, {"enabled": False}),
}


def generate() -> dict:
    records = gen.contract()
    payloads = gen.fixture_payloads()
    cases = []
    for entity, (fixture_name, name_edit, alternative, enabled_edit) in SPECS.items():
        data_class, groups = records[entity]
        base = payloads[fixture_name]
        left = gen.edit(base, entity, groups, name_edit, model.Stamp(gen.T1, 0, gen.MAC))
        right = gen.edit(base, entity, groups, enabled_edit,
                         model.Stamp(gen.T2, 0, gen.PHONE))
        same_clock = gen.edit(base, entity, groups, alternative,
                              model.Stamp(gen.T1, 0, gen.MAC))
        for suffix, old, new, description in (
            ("disjoint_name_then_enabled", left, right,
             "offline name and enable edits converge, delayed enable batch"),
            ("disjoint_enabled_then_name", right, left,
             "same edits converge independent of delayed batch order"),
            ("equal_clock_name_conflict", left, same_clock,
             "same field clock with a different name must quarantine"),
        ):
            decision, merged = model.merge(entity, groups, old, new)
            cases.append({"name": f"{data_class}.{suffix}", "entityType": entity,
                          "dataClass": data_class, "inputValid": True,
                          "description": description, "existing": old, "incoming": new,
                          "expect": {"decision": decision, "merged": merged}})
    return {"schemaVersion": 1,
            "contract": "Ahoi format-3 inventory and asset field merge",
            "generatedBy": "tools/sync_conformance/generate_product_merge_vectors.py",
            "cases": cases}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    rendered = gen.render(generate())
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != rendered:
            print(f"{OUTPUT.relative_to(gen.ROOT)} is stale", file=sys.stderr)
            return 1
        return 0
    OUTPUT.write_text(rendered)
    print(f"wrote {len(generate()['cases'])} cases to {OUTPUT.relative_to(gen.ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
