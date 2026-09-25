#!/usr/bin/env python3
"""Drift gate: sync field groups in config/sync-format.json versus C++ and Swift.

The canonical per-entity field groups live in `config/sync-format.json`
(`records[].fieldGroups`). Chromium's C++ (`FieldNames` in
`sync_field_merge.cc`, entity ids from `EntityType` in `sync_model.h`) and the
Swift Companion (`Set<String>` declarations) each keep a hand-written copy.
This tool reports every copy that differs from the contract. It only reads.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from dataclasses import dataclass

ROOT = pathlib.Path(__file__).resolve().parents[2]
SYNC = "overlay/chromium/src/ahoi/browser/sync"
SWIFT = "apps/AhoiMobile/Sources/AhoiMobileCore"
SPIKE = "spikes/cloudkit/Sources/AhoiCloudKitSpike"

# entity id -> Swift declarations that must equal the contract's field groups.
SWIFT_DECLARATIONS: dict[int, list[tuple[str, str]]] = {
    0: [(f"{SWIFT}/DesktopWirePayloadCodec.swift", "deviceFields"),
        (f"{SWIFT}/CompanionReadModelFieldMerge.swift", "deviceFields"),
        (f"{SWIFT}/CompanionDeviceRevocation.swift", "deviceFields")],
    1: [(f"{SWIFT}/CompanionFieldMerge.swift", "workspaceFields"),
        (f"{SWIFT}/DesktopWirePayloadCodec.swift", "workspaceFields")],
    2: [(f"{SWIFT}/CompanionFieldMerge.swift", "treeNodeFields"),
        (f"{SWIFT}/DesktopWireSharedTabReadPolicy.swift", "treeNodeBaseFields")],
    3: [(f"{SWIFT}/DesktopWirePayloadCodec.swift", "historyFields"),
        (f"{SWIFT}/CompanionReadModelFieldMerge.swift", "historyFields")],
    4: [(f"{SWIFT}/DesktopWireSharedTabReadPolicy.swift", "remoteTabBaseFields"),
        (f"{SWIFT}/CompanionReadModelFieldMerge.swift", "tabFields")],
    5: [(f"{SWIFT}/DesktopWirePayloadCodec.swift", "sessionFields"),
        (f"{SWIFT}/CompanionReadModelFieldMerge.swift", "sessionFields")],
    6: [(f"{SWIFT}/DesktopWirePayloadCodec.swift", "commandFields")],
    7: [(f"{SWIFT}/DesktopWireProductPayloadCodec.swift", "appearanceFields")],
    8: [(f"{SWIFT}/DesktopWireProductPayloadCodec.swift", "permittedSettingFields")],
    9: [(f"{SWIFT}/DesktopWireProductPayloadCodec.swift", "extensionInventoryFields")],
    10: [(f"{SWIFT}/DesktopWireProductPayloadCodec.swift", "developerAssetFields")],
    11: [(f"{SPIKE}/BookmarkModels.swift", "syncFields")],
    12: [(f"{SPIKE}/DeviceCapabilityRecord.swift", "syncFields")],
    13: [(f"{SPIKE}/WorkspaceStructureRecords.swift", "syncFields#1")],
    14: [(f"{SPIKE}/WorkspaceStructureRecords.swift", "syncFields#2")],
}


@dataclass
class Finding:
    entity: str
    source: str
    detail: str

    def render(self) -> str:
        return f"{self.entity}: {self.source}: {self.detail}"


def contract(root: pathlib.Path) -> dict[int, tuple[str, list[str]]]:
    records = json.loads((root / "config/sync-format.json").read_text())["records"]
    return {r["id"]: (r["dataClass"], list(r["fieldGroups"])) for r in records}


def cpp_entity_ids(root: pathlib.Path) -> dict[str, int]:
    text = (root / SYNC / "sync_model.h").read_text()
    body = re.search(r"enum class EntityType\s*\{(.*?)\};", text, re.S).group(1)
    return {name: int(value) for name, value in re.findall(r"(k\w+)\s*=\s*(\d+)", body)}


def cpp_field_groups(root: pathlib.Path) -> dict[int, list[str]]:
    ids = cpp_entity_ids(root)
    text = (root / SYNC / "sync_field_merge.cc").read_text()
    body = re.search(r"Fields FieldNames\(EntityType type\)\s*\{(.*?)\n\}", text, re.S).group(1)
    result = {}
    for name, fields in re.findall(r"case EntityType::(k\w+):\s*return\s*\{(.*?)\};", body, re.S):
        result[ids[name]] = re.findall(r'"([^"]+)"', fields)
    return result


def swift_set(root: pathlib.Path, path: str, symbol: str) -> list[str] | None:
    name, _, occurrence = symbol.partition("#")
    index = int(occurrence or 1) - 1
    text = (root / path).read_text()
    matches = re.findall(rf"static let {name}: Set<String> = \[(.*?)\]", text, re.S)
    if len(matches) <= index:
        return None
    return re.findall(r'"([^"]+)"', matches[index])


def check(root: pathlib.Path = ROOT) -> tuple[list[Finding], int]:
    findings = []
    checked = 0
    expected = contract(root)
    cpp = cpp_field_groups(root)
    for entity_id, (data_class, fields) in sorted(expected.items()):
        label = f"{entity_id} {data_class}"
        if entity_id not in cpp:
            findings.append(Finding(label, "C++ FieldNames", "missing case"))
        elif cpp[entity_id] != fields:
            findings.append(Finding(label, "C++ FieldNames",
                                    f"{cpp[entity_id]} != contract {fields}"))
        checked += 1
        declarations = SWIFT_DECLARATIONS.get(entity_id)
        if not declarations:
            findings.append(Finding(label, "Swift", "no declaration mapped"))
            continue
        for path, symbol in declarations:
            actual = swift_set(root, path, symbol)
            checked += 1
            if actual is None:
                findings.append(Finding(label, f"{path}:{symbol}", "declaration not found"))
            elif set(actual) != set(fields) or len(actual) != len(fields):
                missing = sorted(set(fields) - set(actual))
                extra = sorted(set(actual) - set(fields))
                findings.append(Finding(label, f"{path}:{symbol}",
                                        f"missing {missing}, extra {extra}"))
    for entity_id in sorted(set(cpp) - set(expected)):
        findings.append(Finding(str(entity_id), "C++ FieldNames", "entity not in contract"))
    return findings, checked


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repository", type=pathlib.Path, default=ROOT)
    args = parser.parse_args(argv)
    findings, checked = check(args.repository)
    for finding in findings:
        print("DRIFT:", finding.render())
    print(f"sync field groups: {checked} copies checked, {len(findings)} drift finding(s)")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
