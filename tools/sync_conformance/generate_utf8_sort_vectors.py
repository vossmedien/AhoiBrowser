#!/usr/bin/env python3
"""Supplemental wire-byte sort-key vectors; the accepted 140-case set is unchanged."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import generate_merge_vectors as gen
import merge_model as model

OUTPUT = gen.ROOT / "fixtures/sync-conformance/merge_utf8_sort_keys_v3.json"
DECOMPOSED = "e\u0301"
COMPOSED = "\u00e9"


def generate():
    result = []
    payloads = gen.fixture_payloads()
    for entity in (1, 2):
        data_class, groups = gen.contract()[entity]
        base = payloads[gen.ENTITIES[entity][0]]
        old = gen.edit(base, entity, groups, {"sort_key": DECOMPOSED},
                       model.Stamp(gen.T1, 0, gen.MAC))
        equal = gen.edit(base, entity, groups, {"sort_key": COMPOSED},
                         model.Stamp(gen.T1, 0, gen.MAC))
        newer = gen.edit(base, entity, groups, {"sort_key": COMPOSED},
                         model.Stamp(gen.T2, 0, gen.PHONE))
        for label, existing, incoming in (
            ("equal_clock_decomposed_first", old, equal),
            ("equal_clock_composed_first", equal, old),
            ("newer_utf8_key_wins", old, newer),
            ("older_utf8_key_loses", newer, old),
        ):
            decision, merged = model.merge(entity, groups, existing, incoming)
            result.append({"name": f"utf8_sort_key.{data_class}.{label}", "entityType": entity,
                           "dataClass": data_class, "inputValid": True,
                           "description": "Opaque sort keys compare exact UTF-8 bytes, not Unicode canonical equivalence.",
                           "existing": existing, "incoming": incoming,
                           "expect": {"decision": decision, "merged": merged}})
    return {"schemaVersion": 1, "modelVersion": 3, "cases": result}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    data = json.dumps(generate(), indent=2, ensure_ascii=False) + "\n"
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != data:
            print("UTF-8 sort-key vectors are stale", file=sys.stderr)
            return 1
        return 0
    OUTPUT.write_text(data)
    print(f"wrote 8 supplemental sort-key vectors to {OUTPUT.relative_to(gen.ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
