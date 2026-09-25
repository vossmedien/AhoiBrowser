#!/usr/bin/env python3
"""Generate fixtures/sync-conformance/merge_v3.json.

Each case takes one canonical record of `sync_wire_v3.json`, derives an
existing and an incoming payload, and records the result of
`merge_model.merge`. The model's results are pinned independently by
`tests/repository/test_sync_conformance_merge.py`. `--check` fails when the
committed vectors differ from a fresh generation.
"""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import merge_model as m  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "overlay/chromium/src/ahoi/browser/sync/testdata/sync_wire_v3.json"
OUTPUT = ROOT / "fixtures/sync-conformance/merge_v3.json"

MAC = "a0000000-0000-4000-8000-000000000001"
PHONE = "a0000000-0000-4000-8000-000000000002"
T1, T2, T3 = 11644473604000000, 11644473605000000, 11644473606000000

# entity: (fixture record, first mutable group change, second mutable group change,
#          immutable group change or None)
ABSENT = object()  # removes an optional payload key (the decoder rejects null)
SYSTEM_ACCENT = {"use_system_accent": True, "accent_argb": ABSENT}

# entity: (fixture record, first change, typed alternative of the first change,
#          second change of a different group or None, immutable change or None)
# Every value must pass the wire decoder on both sides; the conformance run of
# 25 September rejected string-suffixed timestamps, enums and JSON text, an
# accent next to the system accent, and any remote-command tombstone.
ENTITIES = {
    1: ("workspace", {"name": "Renamed workspace"}, {"name": "Other workspace"},
        {"icon": "star"}, {"created_at": "11644473601000001"}),
    2: ("tree_folder", {"title": "Renamed folder"}, {"title": "Other folder"},
        {"sort_key": "B"}, {"node_kind": 1}),
    5: ("device_session", {"last_seen": "11644473604000000", "active": False},
        {"last_seen": "11644473604500000", "active": True},
        {"tombstone": True}, {"started_at": "11644473601000001"}),
    # A command is never tombstoned and `request` is immutable, so it has no
    # second mutable group: no disjoint-union cases.
    6: ("remote_command_open_shape_only", {"status": 1}, {"status": 2}, None,
        {"url": "https://example.com/other"}),
    7: ("appearance_custom_accent", {"color_mode": "light"}, {"color_mode": "system"},
        SYSTEM_ACCENT, None),
    8: ("permitted_setting_glass_enabled", {"value_json": "false"},
        {"value_json": "true"}, {"tombstone": True}, {"setting_id": "ahoi.appearance.other"}),
    14: ("archive_single_page", {"restored": True}, {"restored": False},
         {"tombstone": True}, None),
}


def contract() -> dict[int, tuple[str, list[str]]]:
    records = json.loads((ROOT / "config/sync-format.json").read_text())["records"]
    return {r["id"]: (r["dataClass"], r["fieldGroups"]) for r in records}


def fixture_payloads() -> dict[str, dict]:
    records = json.loads(FIXTURE.read_text())["records"]
    return {r["name"]: json.loads(r["payload"]) for r in records}


def group_of(entity: int, key: str, groups: list[str]) -> str:
    for group in groups:
        if key in m.keys_of(entity, group):
            return group
    raise KeyError(key)


def edit(base: dict, entity: int, groups: list[str], values: dict, stamp: m.Stamp,
         record: m.Stamp | None = None) -> dict:
    """Apply payload key changes, stamp their groups and raise the record clock."""
    payload = copy.deepcopy(base)
    for key, value in values.items():
        if value is ABSENT:
            payload.pop(key, None)
        else:
            payload[key] = value
        payload["field_versions"][group_of(entity, key, groups)] = stamp.to_field()
    top = max(record or stamp, m.Stamp.of_record(base))
    payload["version_physical"] = str(top.physical)
    payload["version_logical"] = top.logical
    payload["version_device"] = top.device
    return payload


def cases_for(entity: int, data_class: str, groups: list[str],
              base: dict) -> list[dict]:
    name, first, first_alt, second, immutable = ENTITIES[entity]
    s = lambda physical, device=MAC, logical=0: m.Stamp(physical, logical, device)  # noqa: E731
    cases = [
        ("identical", "identical records", base, base),
        ("incoming_newer", "incoming changes one group with a newer clock",
         base, edit(base, entity, groups, first, s(T1, PHONE))),
        ("incoming_older", "existing change is newer than the incoming change",
         edit(base, entity, groups, first, s(T2)),
         edit(base, entity, groups, first_alt, s(T1, PHONE))),
        ("equal_clock_conflict", "same clock, different value",
         edit(base, entity, groups, first, s(T1)),
         edit(base, entity, groups, first_alt, s(T1))),
        ("device_tiebreak", "same physical time and counter; the larger device id wins",
         edit(base, entity, groups, first, s(T1, MAC)),
         edit(base, entity, groups, first_alt, s(T1, PHONE))),
    ]
    if second is not None:
        cases += [
            ("disjoint_union", "offline edits of different groups converge to their union",
             edit(base, entity, groups, first, s(T1)),
             edit(base, entity, groups, second, s(T2, PHONE))),
            ("logical_overflow_successor", "union successor carries over a full logical counter",
             edit(base, entity, groups, first, s(T1, MAC, m.UINT32_MAX)),
             edit(base, entity, groups, second, s(T1, PHONE, m.UINT32_MAX))),
        ]
    incomplete = copy.deepcopy(edit(base, entity, groups, first, s(T1, PHONE)))
    incomplete["field_versions"].pop(groups[0])
    cases.append(("incomplete_field_map", "incoming lacks one field clock", base, incomplete))
    future = edit(base, entity, groups, first, s(T2, PHONE), record=s(T1, PHONE))
    future["version_physical"] = str(T1)
    cases.append(("field_clock_after_record_clock", "field clock newer than record clock",
                  base, future))
    if immutable:
        cases.append(("immutable_change", "incoming changes an immutable group",
                      base, edit(base, entity, groups, immutable, s(T1, PHONE))))
    if entity == 2:
        cases.append(("move_is_atomic", "location group moves parent and order together",
                      edit(base, entity, groups, {"sort_key": "C"}, s(T1)),
                      edit(base, entity, groups,
                           {"parent_id": "a2000000-0000-4000-8000-0000000000ff",
                            "sort_key": "D"}, s(T2, PHONE))))
    if entity == 6:
        executed = edit(base, entity, groups, {"status": 2}, s(T1))
        cases.append(("terminal_status_kept", "a later non-terminal status never replaces executed",
                      executed, edit(base, entity, groups, {"status": 1}, s(T2, PHONE))))
        cases.append(("status_never_regresses", "a later lower status is ignored",
                      edit(base, entity, groups, {"status": 1}, s(T1)),
                      edit(base, entity, groups, {"status": 0}, s(T2, PHONE))))
    if entity == 7:
        cases.append(("cross_group_union_rejected",
                      "system accent and a later custom accent from two devices; the "
                      "union breaks the accent invariant and is rejected (handoff 012)",
                      edit(base, entity, groups, SYSTEM_ACCENT, s(T2)),
                      edit(base, entity, groups, {"accent_argb": -16776961}, s(T3, PHONE))))
    if entity == 14:
        deleted = edit(base, entity, groups, {"tombstone": True}, s(T2))
        cases.append(("archive_deletion_is_terminal", "a newer untombstone does not revive",
                      deleted, edit(base, entity, groups, {"tombstone": False,
                                                           "restored": True}, s(T3, PHONE))))
    result = []
    for key, description, existing, incoming in cases:
        decision, merged = m.merge(entity, groups, existing, incoming)
        result.append({"name": f"{name}.{key}", "entityType": entity,
                       "dataClass": data_class,
                       "description": description, "existing": existing,
                       "incoming": incoming,
                       "expect": {"decision": decision, "merged": merged}})
    return result


RANDOM_SEED = 153
RANDOM_CASES = 60
# Valid values per payload key for seeded random edits; small pools make equal
# values and equal clocks (conflicts) occur regularly.
RANDOM_VALUES = {
    1: {"name": ["Arbeit", "Privat"], "icon": ["star", "compass"], "sort_key": ["M", "N"],
        "tombstone": [False, True]},
    2: {"title": ["Alpha", "Beta"], "icon": ["folder", "star"], "sort_key": ["A", "B"],
        "tombstone": [False, True]},
    # Accent and system accent change together; the decoder forbids both set.
    7: {"color_mode": ["light", "dark", "system"],
        "accent_state": [SYSTEM_ACCENT, {"use_system_accent": False, "accent_argb": -16776961}]},
    8: {"value_json": ["true", "false"], "tombstone": [False, True]},
}


def random_cases(seed: int = RANDOM_SEED, count: int = RANDOM_CASES) -> list[dict]:
    import random
    rng = random.Random(seed)
    records = contract()
    payloads = fixture_payloads()
    clocks = [T1, T2]
    result = []
    for index in range(count):
        entity = rng.choice(sorted(RANDOM_VALUES))
        data_class, groups = records[entity]
        base = payloads[ENTITIES[entity][0]]
        sides = []
        for device in (MAC, PHONE):
            keys = rng.sample(sorted(RANDOM_VALUES[entity]), rng.randint(1, 2))
            values = {}
            for key in keys:
                choice = rng.choice(RANDOM_VALUES[entity][key])
                values.update(choice if isinstance(choice, dict) else {key: choice})
            # Occasionally both devices share a device id to force equal clocks.
            owner = device if rng.random() > 0.15 else MAC
            sides.append(edit(base, entity, groups, values, m.Stamp(rng.choice(clocks), 0, owner)))
        decision, merged = m.merge(entity, groups, sides[0], sides[1])
        result.append({"name": f"random.{seed}.{index:03d}.{data_class}", "entityType": entity,
                       "dataClass": data_class,
                       "description": f"seeded random concurrent edits (seed {seed})",
                       "existing": sides[0], "incoming": sides[1],
                       "expect": {"decision": decision, "merged": merged}})
    return result


def generate() -> dict:
    records = contract()
    payloads = fixture_payloads()
    cases = []
    for entity in sorted(ENTITIES):
        data_class, groups = records[entity]
        cases.extend(cases_for(entity, data_class, groups, payloads[ENTITIES[entity][0]]))
    cases.extend(random_cases())
    return {
        "schemaVersion": 1,
        "contract": "Ahoi format-3 field merge conformance",
        "generatedBy": "tools/sync_conformance/generate_merge_vectors.py",
        "semantics": "overlay/chromium/src/ahoi/browser/sync/sync_field_merge.cc MergeRecordFields",
        "decisions": ["duplicate", "keepExisting", "acceptIncoming", "mergeFields", "invalid"],
        "comparison": "decode merged payload and compare every field group value and clock "
                      "plus the record clock; key order is irrelevant",
        "cases": cases,
    }


def render(data: dict) -> str:
    return json.dumps(data, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    text = render(generate())
    if args.check:
        current = OUTPUT.read_text() if OUTPUT.exists() else ""
        if current != text:
            print(f"{OUTPUT.relative_to(ROOT)} is stale; regenerate it", file=sys.stderr)
            return 1
        return 0
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(text)
    print(f"wrote {len(json.loads(text)['cases'])} cases to {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
