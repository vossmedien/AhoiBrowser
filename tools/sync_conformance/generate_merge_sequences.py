#!/usr/bin/env python3
"""Generate deterministic multi-operation Format-3 merge sequences for H1.3."""

from __future__ import annotations

import argparse
import copy
import pathlib
import random
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import generate_merge_vectors as gen  # noqa: E402
import merge_model as model  # noqa: E402

OUTPUT = gen.ROOT / "fixtures/sync-conformance/merge_sequences_v3.json"
SEED = 153

# Three distinct first-group values; one valid second-group edit. The seed
# shuffles delivery order and chooses values without changing wire semantics.
SPECS = {
    1: ("workspace", "name", ["Research", "Work", "Archive"],
        [{"icon": "star"}, {"icon": "book"}]),
    2: ("tree_folder", "title", ["Papers", "Ideas", "Notes"],
        [{"sort_key": "B"}, {"sort_key": "Z"}]),
    9: ("extension_inventory", "name", ["Extension A", "Extension B", "Extension C"],
        [{"enabled": False}]),
    10: ("developer_asset_css_opt_in", "name", ["CSS A", "CSS B", "CSS C"],
         [{"enabled": False}]),
    13: ("split_2_panes", "ratios",
         [{"primary": 600000, "secondary": 500000},
          {"primary": 650000, "secondary": 500000},
          {"primary": 700000, "secondary": 500000}],
         [{"topology": {"arrangement": 0, "axis": 1,
                        "member_ids": ["a2000000-0000-4000-8000-000000000002",
                                       "a2000000-0000-4000-8000-000000000003"]}}]),
}


def case(name: str, entity: int, base: dict, operations: list[tuple[str, dict]]) -> dict:
    data_class, groups = gen.contract()[entity]
    current = copy.deepcopy(base)
    steps = []
    for label, incoming in operations:
        decision, merged = model.merge(entity, groups, current, incoming)
        steps.append({"name": label, "writerDevice": incoming["version_device"],
                      "incoming": incoming, "inputValid": True,
                      "expect": {"decision": decision, "merged": merged}})
        if decision != "invalid":
            current = merged
    assert steps[-1]["expect"]["decision"] != "invalid"
    return {"name": name, "entityType": entity, "dataClass": data_class,
            "initial": base, "steps": steps,
            "expect": {"decision": steps[-1]["expect"]["decision"],
                       "merged": current}}


def seeded_cases(seed: int = SEED, repetitions: int = 3) -> list[dict]:
    rng = random.Random(seed)
    payloads = gen.fixture_payloads()
    result = []
    for index in range(repetitions):
        for entity in sorted(SPECS):
            fixture_name, key, first_values, second_values = SPECS[entity]
            base = payloads[fixture_name]
            groups = gen.contract()[entity][1]
            first, conflict, later = rng.sample(first_values, 3)
            a = gen.edit(base, entity, groups, {key: first},
                         model.Stamp(gen.T1, 0, gen.MAC))
            b = gen.edit(base, entity, groups, rng.choice(second_values),
                         model.Stamp(gen.T2, 0, gen.PHONE))
            equal_clock_conflict = gen.edit(base, entity, groups, {key: conflict},
                                            model.Stamp(gen.T1, 0, gen.MAC))
            later_a = gen.edit(base, entity, groups, {key: later},
                               model.Stamp(gen.T3, 0, gen.MAC))
            deliveries = [("first_group", a), ("second_group", b)]
            rng.shuffle(deliveries)
            deliveries.extend([
                ("quarantine_equal_clock", equal_clock_conflict),
                ("duplicate_delayed_batch", b),
                ("later_first_group", later_a),
                ("stale_baseline", base),
            ])
            result.append(case(f"sequence.{seed}.{index:02d}.{entity}", entity,
                               base, deliveries))
    return result


def fixed_cases() -> list[dict]:
    records = gen.contract()
    payloads = gen.fixture_payloads()
    s = model.Stamp
    workspace = payloads["workspace"]
    fields = records[1][1]
    merge = gen.edit(workspace, 1, fields,
                     {"tombstone": True, "merged_into": gen.MERGE_TARGET},
                     s(gen.T1, 0, gen.MAC))
    rename = gen.edit(workspace, 1, fields, {"name": "Late rename"},
                      s(gen.T2, 0, gen.PHONE))
    conflicting_route = gen.edit(workspace, 1, fields,
                                 {"tombstone": True,
                                  "merged_into": gen.OTHER_MERGE_TARGET},
                                 s(gen.T1, 0, gen.MAC))
    undo = gen.edit(workspace, 1, fields,
                    {"tombstone": False, "merged_into": gen.ABSENT},
                    s(gen.T3, 0, gen.PHONE))

    command = payloads["remote_command_open_shape_only"]
    fields = records[6][1]
    executed = gen.edit(command, 6, fields, {"status": 2}, s(gen.T1, 0, gen.MAC))
    regressed = gen.edit(command, 6, fields, {"status": 1}, s(gen.T2, 0, gen.PHONE))
    conflicting_status = gen.edit(command, 6, fields, {"status": 3}, s(gen.T1, 0, gen.MAC))

    archive = payloads["archive_single_page"]
    fields = records[14][1]
    restored = gen.edit(archive, 14, fields, {"restored": True}, s(gen.T1, 0, gen.MAC))
    deleted = gen.edit(archive, 14, fields, {"tombstone": True},
                       s(gen.T2, 0, gen.PHONE))
    revival = gen.edit(archive, 14, fields, {"tombstone": False},
                       s(gen.T3, 0, gen.MAC))

    return [
        case("sequence.workspace.merge_rename_conflict_undo", 1, workspace, [
            ("merge_tombstone", merge), ("late_remote_rename", rename),
            ("quarantine_equal_clock_route", conflicting_route),
            ("newer_undo", undo), ("stale_merge_replay", merge),
            ("stale_baseline", workspace),
        ]),
        case("sequence.remoteCommand.terminal_status", 6, command, [
            ("executed", executed), ("late_nonterminal_status", regressed),
            ("quarantine_equal_clock_status", conflicting_status),
            ("stale_baseline", command),
        ]),
        case("sequence.tabArchiveEntry.terminal_delete", 14, archive, [
            ("restored", restored), ("deleted", deleted),
            ("late_revival", revival), ("stale_baseline", archive),
        ]),
    ]


def generate(seed: int = SEED) -> dict:
    cases = seeded_cases(seed) + fixed_cases()
    return {"schemaVersion": 1,
            "kind": "ahoi-format3-merge-sequences",
            "contract": "Ahoi Format-3 sequential field merge conformance",
            "generatedBy": "tools/sync_conformance/generate_merge_sequences.py",
            "seed": seed,
            "cases": cases}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=SEED)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    rendered = gen.render(generate(args.seed))
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != rendered:
            print(f"{OUTPUT.relative_to(gen.ROOT)} is stale", file=sys.stderr)
            return 1
        return 0
    OUTPUT.write_text(rendered)
    print(f"wrote {len(generate(args.seed)['cases'])} sequences to {OUTPUT.relative_to(gen.ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
