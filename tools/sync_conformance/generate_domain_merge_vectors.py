#!/usr/bin/env python3
"""Generate shared Format-3 merge cases for published domain-value groups.

The 140 standard vectors never vary Workspace archive policy/accent/
modified_at, the TreeNode page target, Home, temporary state or accent, or the
extension setup/storage values carried by PermittedSetting. These cases use
only contract-valid values from `config/sync-format.json` and records of the
canonical wire fixture. Multi-key groups (page target, Home) must move
atomically, and a union that breaks the new-tab/temporary invariant must be
rejected like the appearance accent union (handoff 012).
"""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import generate_merge_vectors as gen  # noqa: E402
import merge_model as model  # noqa: E402

OUTPUT = gen.ROOT / "fixtures/sync-conformance/merge_domain_groups_v3.json"

ABSENT = gen.ABSENT
BLUE = -16776961  # 0xff0000ff as the signed int32 the wire carries
HOME_OTHER_WEB = {"home_url": "https://example.com/home", "home_target_kind": 0,
                  "home_local_scheme": None}
HOME_LOCAL_FILE = {"home_url": "", "home_target_kind": 2, "home_local_scheme": "file"}
HOME_CLEARED = {"home_url": None, "home_target_kind": None, "home_local_scheme": None}
TARGET_OTHER_WEB = {"url": "https://example.com/other", "target_kind": 0,
                    "local_scheme": ABSENT}
TARGET_LOCAL_FILE = {"url": "", "target_kind": 2, "local_scheme": "file"}
TARGET_NEW_TAB = {"url": "", "target_kind": 1, "local_scheme": ABSENT}
DESIRED = '{"enabled":%s,"installed":%s,"source":"chromeWebStore"}'
DISABLED = DESIRED % ("false", "true")
UNINSTALLED = DESIRED % ("false", "false")


def stamp(physical: int, device: str = gen.MAC) -> model.Stamp:
    return model.Stamp(physical, 0, device)


# (name, fixture record, entity, description,
#  (existing edit, stamp) or None for the base, (incoming edit, stamp))
SPECS = [
    ("workspace.archive_policy_newer", "workspace", 1,
     "a newer archive policy replaces never",
     None, ({"archive_policy": 3}, stamp(gen.T1, gen.PHONE))),
    ("workspace.archive_policy_rename_union", "workspace", 1,
     "an offline rename and a later policy change converge",
     ({"name": "Renamed workspace"}, stamp(gen.T1)),
     ({"archive_policy": 4}, stamp(gen.T2, gen.PHONE))),
    ("workspace.archive_policy_equal_clock_conflict", "workspace", 1,
     "different policies under the same field clock are rejected",
     ({"archive_policy": 1}, stamp(gen.T1)), ({"archive_policy": 2}, stamp(gen.T1))),
    ("workspace.accent_icon_union", "workspace", 1,
     "a custom accent and a later icon edit converge",
     ({"accent_argb": BLUE}, stamp(gen.T1)), ({"icon": "star"}, stamp(gen.T2, gen.PHONE))),
    ("workspace.accent_removal_newer", "workspace", 1,
     "a newer absent accent removes the older custom accent",
     ({"accent_argb": BLUE}, stamp(gen.T1)),
     ({"accent_argb": ABSENT}, stamp(gen.T2, gen.PHONE))),
    ("workspace.modified_at_follows_newer_edit", "workspace", 1,
     "edits that also stamp modified_at keep each group and the newer modification time",
     ({"name": "Renamed workspace", "modified_at": str(gen.T1)}, stamp(gen.T1)),
     ({"archive_policy": 2, "modified_at": str(gen.T2)}, stamp(gen.T2, gen.PHONE))),
    ("tree.home_target_newer_atomic", "tree_saved_web", 2,
     "a newer local-only Home replaces all three Home keys together",
     None, (HOME_LOCAL_FILE, stamp(gen.T1, gen.PHONE))),
    ("tree.home_target_title_union", "tree_saved_web", 2,
     "an offline rename and a later Home edit converge",
     ({"title": "Renamed page"}, stamp(gen.T1)), (HOME_OTHER_WEB, stamp(gen.T2, gen.PHONE))),
    ("tree.home_target_newer_clear", "tree_saved_web", 2,
     "a newer Home removal (three nulls) replaces an older Home edit",
     (HOME_OTHER_WEB, stamp(gen.T1)), (HOME_CLEARED, stamp(gen.T2, gen.PHONE))),
    ("tree.home_target_stale_clear_loses", "tree_saved_web", 2,
     "an older Home removal cannot erase the newer Home edit",
     (HOME_OTHER_WEB, stamp(gen.T2)), (HOME_CLEARED, stamp(gen.T1, gen.PHONE))),
    ("tree.home_target_equal_clock_conflict", "tree_saved_web", 2,
     "different Homes under the same field clock are rejected",
     (HOME_OTHER_WEB, stamp(gen.T1)), (HOME_LOCAL_FILE, stamp(gen.T1))),
    ("tree.dormant_home_on_temporary_union", "tree_saved_web", 2,
     "a concurrent Home edit is retained, dormant, on a page made temporary",
     ({"is_temporary": True}, stamp(gen.T1)), (HOME_OTHER_WEB, stamp(gen.T2, gen.PHONE))),
    ("tree.page_target_newer_atomic", "tree_saved_web", 2,
     "a newer local-only target replaces url, kind and scheme together",
     (TARGET_OTHER_WEB, stamp(gen.T1)), (TARGET_LOCAL_FILE, stamp(gen.T2, gen.PHONE))),
    ("tree.page_target_title_union", "tree_saved_web", 2,
     "an offline rename and a later local-only target converge",
     ({"title": "Renamed page"}, stamp(gen.T1)), (TARGET_LOCAL_FILE, stamp(gen.T2, gen.PHONE))),
    ("tree.page_target_equal_clock_conflict", "tree_saved_web", 2,
     "different targets under the same field clock are rejected",
     (TARGET_OTHER_WEB, stamp(gen.T1)), (TARGET_LOCAL_FILE, stamp(gen.T1))),
    ("tree.temporary_page_saved_newer", "tree_temporary_web", 2,
     "saving a temporary page is a newer is_temporary edit",
     None, ({"is_temporary": False}, stamp(gen.T1, gen.PHONE))),
    ("tree.new_tab_union_requires_temporary", "tree_temporary_web", 2,
     "an explicit new-tab target and a concurrent save cannot form a saved new tab",
     (TARGET_NEW_TAB, stamp(gen.T1)), ({"is_temporary": False}, stamp(gen.T2, gen.PHONE))),
    ("tree.accent_icon_union", "tree_saved_web", 2,
     "a page accent and a later icon edit converge",
     ({"accent_argb": BLUE}, stamp(gen.T1)), ({"icon": "star"}, stamp(gen.T2, gen.PHONE))),
    ("setting.extension_desired_newer_disable", "extension_setup_web_store", 8,
     "a newer desired state disables the installed extension",
     None, ({"value_json": DISABLED}, stamp(gen.T1, gen.PHONE))),
    ("setting.extension_desired_older_uninstall_loses", "extension_setup_web_store", 8,
     "an older uninstall cannot replace a newer disable",
     ({"value_json": DISABLED}, stamp(gen.T2)),
     ({"value_json": UNINSTALLED}, stamp(gen.T1, gen.PHONE))),
    ("setting.extension_desired_equal_clock_conflict", "extension_setup_web_store", 8,
     "disable and uninstall under the same field clock are rejected",
     ({"value_json": DISABLED}, stamp(gen.T1)), ({"value_json": UNINSTALLED}, stamp(gen.T1))),
    ("setting.extension_storage_newer_reset", "extension_storage_vimium_boolean", 8,
     "a newer reset (JSON null) replaces the boolean value",
     None, ({"value_json": "null"}, stamp(gen.T1, gen.PHONE))),
    ("setting.extension_storage_stale_reset_loses", "extension_storage_vimium_boolean", 8,
     "an older reset cannot erase the newer boolean value",
     ({"value_json": "true"}, stamp(gen.T2)), ({"value_json": "null"}, stamp(gen.T1, gen.PHONE))),
    ("setting.extension_storage_equal_clock_conflict", "extension_storage_vimium_boolean", 8,
     "true and reset under the same field clock are rejected",
     ({"value_json": "true"}, stamp(gen.T1)), ({"value_json": "null"}, stamp(gen.T1))),
]


def generate() -> dict:
    records = gen.contract()
    payloads = gen.fixture_payloads()
    cases = []
    for name, fixture_name, entity, description, existing_edit, incoming_edit in SPECS:
        data_class, groups = records[entity]
        base = payloads[fixture_name]
        existing = base if existing_edit is None else gen.edit(
            base, entity, groups, existing_edit[0], existing_edit[1])
        incoming = gen.edit(base, entity, groups, incoming_edit[0], incoming_edit[1])
        decision, merged = model.merge(entity, groups, existing, incoming)
        cases.append({"name": name, "entityType": entity, "dataClass": data_class,
                      "inputValid": True, "description": description,
                      "existing": existing, "incoming": incoming,
                      "expect": {"decision": decision, "merged": merged}})
    return {"schemaVersion": 1,
            "contract": "Ahoi Format-3 domain-value field-group merge conformance",
            "generatedBy": "tools/sync_conformance/generate_domain_merge_vectors.py",
            "cases": cases}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
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
