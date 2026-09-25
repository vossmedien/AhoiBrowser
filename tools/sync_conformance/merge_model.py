"""Executable specification of Chromium's `MergeRecordFields` (sync_field_merge.cc).

Works on canonical format-3 wire payloads (the JSON objects inside
`sync_wire_v3.json`). It exists only to compute the expected results of the
merge conformance vectors; the C++ and Swift implementations are the products
under test. Every rule cites the C++ it mirrors.
"""

from __future__ import annotations

import copy
from dataclasses import dataclass
from typing import Optional

MODEL_VERSION = 3
MIN_PHYSICAL = 11644473600000000  # kMinimumSyncClockPhysicalUs
UINT32_MAX = 4294967295
INT64_MAX = 2**63 - 1

# Payload keys of each field group for the entity types covered by the vectors.
# Groups missing from a table map to the payload key of the same name.
GROUP_KEYS: dict[int, dict[str, list[str]]] = {
    1: {},  # workspace
    2: {"location": ["workspace_id", "parent_id", "sort_key"],
        "kind": ["node_kind"],
        "home_target": ["home_target_kind", "home_url", "home_local_scheme"]},
    5: {"liveness": ["last_seen", "active"]},  # deviceSession
    6: {"request": ["command_kind", "expires_at", "issued_at", "nonce", "signature",
                    "source_device_id", "target_device_id", "url", "workspace_id",
                    "tab_id"],
        "status": ["status", "result"]},  # remoteCommand
    7: {},  # appearance
    8: {},  # permittedSetting
    14: {"state": ["reason", "archived_at", "restored"]},  # tabArchiveEntry
}

# IsImmutableField
IMMUTABLE = {1: {"created_at"}, 2: {"kind", "created_at"},
             5: {"device_id", "started_at"}, 6: {"request"}, 7: set(),
             8: {"setting_id"}, 14: set()}

TERMINAL_COMMAND_STATUS = {2, 3}  # kExecuted, kFailed


class Invalid(Exception):
    pass


@dataclass(frozen=True, order=True)
class Stamp:
    physical: int
    logical: int
    device: str

    @classmethod
    def from_field(cls, value: dict) -> "Stamp":
        return cls(int(value["physical"]), int(value["logical"]), value["device"])

    @classmethod
    def of_record(cls, payload: dict) -> "Stamp":
        return cls(int(payload["version_physical"]), int(payload["version_logical"]),
                   payload["version_device"])

    def to_field(self) -> dict:
        return {"device": self.device, "logical": self.logical,
                "physical": str(self.physical)}

    def valid(self) -> bool:  # IsValidSyncClock
        return (MIN_PHYSICAL <= self.physical <= INT64_MAX
                and 0 <= self.logical <= UINT32_MAX and bool(self.device))


def keys_of(entity: int, group: str) -> list[str]:
    return GROUP_KEYS[entity].get(group, [group])


def group_value(entity: int, payload: dict, group: str) -> tuple:
    # An absent optional key equals an explicit null (e.g. accent_argb, parent_id).
    return tuple(payload.get(key) for key in keys_of(entity, group))


def copy_group(entity: int, source: dict, target: dict, group: str) -> None:
    for key in keys_of(entity, group):
        if key in source:
            target[key] = copy.deepcopy(source[key])
        else:
            target.pop(key, None)


def check_complete(payload: dict, groups: list[str]) -> None:
    """HasCompleteFieldVersions: current model and exactly the known clocks."""
    if payload.get("model_version") != MODEL_VERSION or payload.get("version_model") != MODEL_VERSION:
        raise Invalid("current complete field maps required")
    versions = payload.get("field_versions") or {}
    record = Stamp.of_record(payload)
    if set(versions) != set(groups) or not record.valid():
        raise Invalid("current complete field maps required")
    for value in versions.values():
        stamp = Stamp.from_field(value)
        if not stamp.valid() or stamp > record:
            raise Invalid("current complete field maps required")


def next_merge_stamp(stamp: Stamp) -> Stamp:
    if stamp.logical < UINT32_MAX:
        return Stamp(stamp.physical, stamp.logical + 1, stamp.device)
    if stamp.physical == INT64_MAX:
        raise Invalid("merge clock exhausted")
    return Stamp(stamp.physical + 1, 0, stamp.device)


def projected(entity: int, payload: dict, groups: list[str]) -> list:
    return [(group_value(entity, payload, g), Stamp.from_field(payload["field_versions"][g]))
            for g in groups]


def merge(entity: int, groups: list[str], existing: dict,
          incoming: dict) -> tuple[str, Optional[dict]]:
    """Return (decision, merged payload or None) like MergeRecordFields."""
    try:
        if existing.get("id") != incoming.get("id"):
            raise Invalid("merge identity mismatch")
        check_complete(existing, groups)
        check_complete(incoming, groups)
        for group in groups:
            if group in IMMUTABLE[entity] and \
                    group_value(entity, existing, group) != group_value(entity, incoming, group):
                raise Invalid("immutable field conflict")

        merged = copy.deepcopy(existing)
        for group in groups:
            old = Stamp.from_field(existing["field_versions"][group])
            new = Stamp.from_field(incoming["field_versions"][group])
            if new == old:
                if group_value(entity, existing, group) != group_value(entity, incoming, group):
                    raise Invalid("equal field clock conflict")
                continue
            if entity == 14 and group == "tombstone" and \
                    bool(existing.get("tombstone")) != bool(incoming.get("tombstone")):
                # Archive deletion is terminal; restore never untombstones.
                if incoming.get("tombstone"):
                    copy_group(entity, incoming, merged, group)
                    merged["field_versions"][group] = new.to_field()
                continue
            keep_status = (entity == 6 and group == "status" and (
                existing.get("status") in TERMINAL_COMMAND_STATUS
                or incoming.get("status", 0) < existing.get("status", 0)))
            if new > old and not keep_status:
                copy_group(entity, incoming, merged, group)
                merged["field_versions"][group] = new.to_field()

        top = max(Stamp.of_record(existing), Stamp.of_record(incoming))
        same_old = projected(entity, merged, groups) == projected(entity, existing, groups)
        same_new = projected(entity, merged, groups) == projected(entity, incoming, groups)
        if same_old and same_new:
            decision = "duplicate"
        elif same_old:
            decision = "keepExisting"
        elif same_new:
            decision = "acceptIncoming"
        else:
            decision = "mergeFields"
            top = next_merge_stamp(top)
        merged["version_physical"] = str(top.physical)
        merged["version_logical"] = top.logical
        merged["version_device"] = top.device
        return decision, merged
    except Invalid:
        return "invalid", None
