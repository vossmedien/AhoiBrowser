#!/usr/bin/env python3
"""Generate shared Format-3 merge cases for six unexercised entity classes."""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import generate_merge_vectors as gen  # noqa: E402
import merge_model as model  # noqa: E402

OUTPUT = gen.ROOT / "fixtures/sync-conformance/merge_remaining_entities_v3.json"

# Each value changes only a valid mutable field group. DeviceCapability clocks
# must remain bound to their authenticated DeviceID, so it uses MAC on both
# sides; the other records use two writer IDs to exercise offline union.
SPECS = {
    0: ("device_mac", {"display_name": "Renamed Mac"},
        {"display_name": "Alternative Mac"},
        {"last_seen": str(gen.T2)}, True),
    3: ("history_visit", {"title": "Renamed visit"},
        {"title": "Alternative visit"}, {"tombstone": True}, False),
    4: ("presence_saved_web", {"title": "Renamed shared tab"},
        {"title": "Alternative shared tab"}, {"pinned": False}, False),
    11: ("bookmark_url_nested", {"title": "Renamed bookmark"},
         {"title": "Alternative bookmark"}, {"sort_key": "B"}, False),
    12: ("device_capability_mac", {"features": ["alpha-feature", "shared-normal-tabs-v3"]},
         {"features": ["beta-feature", "shared-normal-tabs-v3"]},
         {"tombstone": True}, True),
    13: ("split_2_panes", {"ratios": {"primary": 600000, "secondary": 500000}},
         {"ratios": {"primary": 700000, "secondary": 500000}},
         {"topology": {"arrangement": 0, "axis": 1,
                       "member_ids": ["a2000000-0000-4000-8000-000000000002",
                                      "a2000000-0000-4000-8000-000000000003"]}}, False),
}


def generate() -> dict:
    records = gen.contract()
    payloads = gen.fixture_payloads()
    cases = []
    for entity, (fixture_name, first_edit, alternative, second_edit, own_writer) in SPECS.items():
        data_class, groups = records[entity]
        base = payloads[fixture_name]
        first = gen.edit(base, entity, groups, first_edit, model.Stamp(gen.T1, 0, gen.MAC))
        second = gen.edit(base, entity, groups, second_edit,
                          model.Stamp(gen.T2, 0, gen.MAC if own_writer else gen.PHONE))
        conflict = gen.edit(base, entity, groups, alternative,
                            model.Stamp(gen.T1, 0, gen.MAC))
        stale = gen.edit(base, entity, groups, alternative,
                         model.Stamp(gen.T1, 0, gen.PHONE if not own_writer else gen.MAC))
        sequences = (
            ("identical", base, base, "identical complete records"),
            ("newer", base, first, "newer edit to one mutable group"),
            ("delayed_older_group", second, stale,
             "older incoming record still contributes its separately newer field group"),
            ("disjoint", first, second, "two different mutable groups converge"),
            ("disjoint_reversed", second, first, "same groups in reverse arrival order"),
            ("equal_clock_conflict", first, conflict,
             "different values under the same field clock must be rejected"),
        )
        for suffix, existing, incoming, description in sequences:
            decision, merged = model.merge(entity, groups, existing, incoming)
            cases.append({"name": f"{data_class}.{suffix}", "entityType": entity,
                          "dataClass": data_class, "inputValid": True,
                          "description": description, "existing": existing,
                          "incoming": incoming,
                          "expect": {"decision": decision, "merged": merged}})
    return {"schemaVersion": 1,
            "contract": "Ahoi Format-3 remaining entity merge conformance",
            "generatedBy": "tools/sync_conformance/generate_remaining_merge_vectors.py",
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
