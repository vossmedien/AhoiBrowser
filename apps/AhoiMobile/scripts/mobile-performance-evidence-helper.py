#!/usr/bin/env python3
"""Metadata, raw-export parsing, and privacy guards for Mobile evidence."""

from __future__ import annotations

import csv
import hashlib
import json
import math
import os
import pathlib
import plistlib
import subprocess
import sys
import xml.etree.ElementTree as ET
from typing import Any

MIB = 1024 * 1024
STAGE_CONTRACTS = {
    "launch-cold": ("App Launch", "life-cycle-period", "idle", False),
    "launch-warm-cache": ("App Launch", "life-cycle-period", "idle", False),
    "memory-normal-1": ("Activity Monitor", "sysmon-process", "idle", False),
    "memory-normal-5": ("Activity Monitor", "sysmon-process", "idle", False),
    "memory-normal-20-discard-restore": (
        "Activity Monitor", "sysmon-process", "discard-restore", False
    ),
    "memory-private-1": ("Activity Monitor", "sysmon-process", "idle", False),
    "memory-private-5": ("Activity Monitor", "sysmon-process", "idle", False),
    "memory-private-20-discard-restore": (
        "Activity Monitor", "sysmon-process", "discard-restore", False
    ),
    "idle-resources": ("Activity Monitor", "sysmon-process", "idle", False),
    "idle-network": ("Network", "har", "idle", False),
    "controller-pressure-policy": (
        "Activity Monitor", "sysmon-process", "lifecycle-flush", False
    ),
    "scroll-motion-standard": (
        "Animation Hitches", "hitches-summary", "scroll", False
    ),
    "scroll-motion-reduced": (
        "Animation Hitches", "hitches-summary", "scroll", True
    ),
}
MARKER_SAMPLE_KEYS = {
    "sessionReadyMilliseconds",
    "sessionFlushMilliseconds",
    "sessionFlushFailureCounts",
    "livePagesForeground",
    "livePagesBackground",
    "livePagesMemoryWarning",
    "discardMilliseconds",
    "restoreMilliseconds",
    "backgroundPolicyMilliseconds",
    "foregroundRestoreMilliseconds",
}
MARKER_SAMPLE_AUTHORITY = {
    "launch-cold": {"sessionReadyMilliseconds"},
    "launch-warm-cache": {"sessionReadyMilliseconds"},
    "memory-normal-20-discard-restore": {
        "discardMilliseconds", "restoreMilliseconds",
    },
    "memory-private-20-discard-restore": {
        "discardMilliseconds", "restoreMilliseconds",
    },
    "controller-pressure-policy": {
        "sessionFlushMilliseconds", "sessionFlushFailureCounts",
        "livePagesForeground", "livePagesBackground", "livePagesMemoryWarning",
        "backgroundPolicyMilliseconds", "foregroundRestoreMilliseconds",
    },
}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    if path.is_dir():
        digest.update(b"ahoi-mobile-tree-sha256-v1\0")
        children = sorted(path.rglob("*"), key=lambda item: item.relative_to(path).as_posix())
        for child in children:
            relative = child.relative_to(path).as_posix().encode()
            if child.is_symlink():
                digest.update(b"L\0" + relative + b"\0" + os.readlink(child).encode() + b"\0")
            elif child.is_file():
                digest.update(b"F\0" + relative + b"\0" + bytes.fromhex(sha256(child)))
            elif child.is_dir():
                digest.update(b"D\0" + relative + b"\0")
    else:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    return digest.hexdigest()


def write_json(path: pathlib.Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def read_json(path: pathlib.Path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return None


def is_number(value: Any) -> bool:
    return type(value) in {int, float} and math.isfinite(float(value)) and value >= 0


def validate_marker_payload(payload, scenario, nonce, workload) -> None:
    expected = {
        "schemaVersion", "scenario", "nonce", "workload", "status", "reduceMotion",
        "normalTabCount", "privateTabCount", "livePageCount", "samples",
    }
    if not isinstance(payload, dict) or set(payload) != expected:
        raise ValueError("workload marker has an unexpected privacy surface")
    contract = STAGE_CONTRACTS.get(scenario)
    if contract is None or contract[2] != workload:
        raise ValueError("workload marker scenario/workload is unsupported")
    fixed = {
        "schemaVersion": 2, "scenario": scenario, "nonce": nonce,
        "workload": workload, "status": "completed", "reduceMotion": contract[3],
    }
    if any(payload.get(key) != value for key, value in fixed.items()):
        raise ValueError("workload marker does not prove the completed scenario")
    for key in ("normalTabCount", "privateTabCount", "livePageCount"):
        value = payload.get(key)
        if type(value) is not int or not 0 <= value <= 20:
            raise ValueError(f"workload marker has an invalid bounded count: {key}")
    counts = {
        "memory-normal-1": ("normalTabCount", 1),
        "memory-normal-5": ("normalTabCount", 5),
        "memory-normal-20-discard-restore": ("normalTabCount", 20),
        "memory-private-1": ("privateTabCount", 1),
        "memory-private-5": ("privateTabCount", 5),
        "memory-private-20-discard-restore": ("privateTabCount", 20),
        "controller-pressure-policy": ("normalTabCount", 20),
    }
    expectation = counts.get(scenario)
    if expectation and payload[expectation[0]] != expectation[1]:
        raise ValueError("workload marker does not prove the requested tab scale")
    samples = payload.get("samples")
    if not isinstance(samples, dict) or set(samples) != MARKER_SAMPLE_KEYS:
        raise ValueError("workload marker samples have an unexpected privacy surface")
    for key, values in samples.items():
        if not isinstance(values, list) or len(values) > 100:
            raise ValueError(f"workload marker sample array is invalid: {key}")
        if any(not is_number(value) for value in values):
            raise ValueError(f"workload marker sample array is invalid: {key}")
    authority = MARKER_SAMPLE_AUTHORITY.get(scenario, set())
    unauthorized = sorted(
        key for key, values in samples.items() if key not in authority and values
    )
    if unauthorized:
        raise ValueError(
            "workload marker contains samples outside scenario authority: "
            + ", ".join(unauthorized)
        )


def validate_workload_marker(arguments: list[str]) -> None:
    marker_path, scenario, nonce, workload = arguments
    payload = read_json(pathlib.Path(marker_path))
    try:
        validate_marker_payload(payload, scenario, nonce, workload)
    except ValueError as error:
        raise SystemExit(str(error)) from error


def validate_plist(path: pathlib.Path) -> None:
    with path.open("rb") as stream:
        payload = plistlib.load(stream)
    supported = payload.get("CFBundleSupportedPlatforms")
    platform_name = payload.get("DTPlatformName")
    families = payload.get("UIDeviceFamily")
    if supported not in (["iPhoneSimulator"], ["iPhoneOS"]):
        raise SystemExit("CFBundleSupportedPlatforms must identify iPhoneSimulator or iPhoneOS")
    if platform_name not in {"iphonesimulator", "iphoneos"}:
        raise SystemExit("DTPlatformName must identify iphonesimulator or iphoneos")
    if not isinstance(families, list) or not families or any(type(item) is not int or item not in {1, 2} for item in families):
        raise SystemExit("UIDeviceFamily must contain only iPhone/iPad identifiers")
    print(supported[0])
    print(platform_name)
    print(",".join(str(item) for item in families))


def write_candidate(arguments: list[str]) -> None:
    (
        output, app_path, bundle_id, executable_name, binary_path, source_commit,
        marketing_version, build_number, build_mode, optimization_level, binary_platform,
        supported_platform, device_families, architectures, signature_kind,
        team_identifier, device_kind,
    ) = arguments
    binary, app = pathlib.Path(binary_path), pathlib.Path(app_path)
    write_json(pathlib.Path(output), {
        "appPath": app_path,
        "bundleId": bundle_id,
        "executableName": executable_name,
        "executablePath": binary_path,
        "sourceCommit": source_commit,
        "marketingVersion": marketing_version,
        "buildNumber": build_number,
        "buildMode": build_mode,
        "optimizationLevel": optimization_level,
        "binaryPlatform": binary_platform,
        "supportedPlatform": supported_platform,
        "deviceFamilies": [int(item) for item in device_families.split(",")],
        "architectures": architectures.split(),
        "signing": {
            "kind": signature_kind,
            "teamIdentifier": None if team_identifier == "not set" else team_identifier,
            "boundDeviceKind": device_kind,
            "verifiedStrictly": True,
        },
        "executableSha256": sha256(binary),
        "infoPlistSha256": sha256(app / "Info.plist"),
        "appBundleTreeSha256": sha256(app),
    })


def xctrace_matches(udid: str, inventory_path: pathlib.Path) -> list[dict]:
    matches, section = [], None
    for raw_line in inventory_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if line.startswith("== ") and line.endswith(" =="):
            section = line.removeprefix("== ").removesuffix(" ==")
        elif f"({udid})" in line:
            matches.append({"descriptor": line, "section": section})
    return matches


def simulator_record(udid: str, inventory_path: pathlib.Path):
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    for runtime, devices in inventory.get("devices", {}).items():
        for device in devices:
            if device.get("udid") != udid:
                continue
            if not device.get("isAvailable") or "iOS" not in runtime:
                raise SystemExit("requested simulator is unavailable or not iOS/iPadOS")
            kind = device.get("deviceTypeIdentifier", "")
            if ".iPhone-" in kind:
                family = "iPhone"
            elif ".iPad-" in kind:
                family = "iPad"
            else:
                raise SystemExit("requested simulator is not an iPhone or iPad")
            return {
                "runtimeIdentifier": runtime, "name": device.get("name"),
                "state": device.get("state"), "isAvailable": device.get("isAvailable"),
                "deviceTypeIdentifier": kind, "deviceFamily": family,
            }
    return None


def physical_record(udid: str, inventory_path: pathlib.Path):
    if not inventory_path.is_file():
        raise SystemExit("physical iOS classification requires CoreDevice JSON")
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    for device in inventory.get("result", {}).get("devices", []):
        hardware = device.get("hardwareProperties", {})
        if udid not in {device.get("identifier"), hardware.get("udid")}:
            continue
        family = hardware.get("deviceType")
        if hardware.get("platform") != "iOS" or hardware.get("reality") != "physical" or family not in {"iPhone", "iPad"}:
            raise SystemExit("requested target is not a physical iOS/iPadOS iPhone or iPad")
        properties = device.get("deviceProperties", {})
        return {
            "name": properties.get("name"), "osVersion": properties.get("osVersionNumber"),
            "deviceFamily": family, "marketingName": hardware.get("marketingName"),
            "platform": hardware.get("platform"), "reality": hardware.get("reality"),
            "productType": hardware.get("productType"),
        }
    raise SystemExit("requested non-simulator UDID is not physical iOS/iPadOS; host Macs are forbidden")


def write_device(arguments: list[str]) -> None:
    output, udid, xctrace_path, simctl_path, device_control_path = arguments
    matches = xctrace_matches(udid, pathlib.Path(xctrace_path))
    if not matches:
        raise SystemExit("requested UDID is absent from xctrace device inventory")
    if any(item["section"] == "Devices Offline" for item in matches):
        raise SystemExit("requested physical device is offline")
    simulator = simulator_record(udid, pathlib.Path(simctl_path))
    if simulator and not any(item["section"] == "Simulators" for item in matches):
        raise SystemExit("simulator classification disagrees with xctrace")
    physical = None if simulator else physical_record(udid, pathlib.Path(device_control_path))
    write_json(pathlib.Path(output), {
        "udid": udid,
        "xctraceInventoryMatches": matches,
        "kind": "simulator" if simulator else "physical",
        "platform": "iOSSimulator" if simulator else "iOS",
        "deviceFamily": simulator["deviceFamily"] if simulator else physical["deviceFamily"],
        "simulator": simulator,
        "physical": physical,
    })


def log_text(path: str) -> str:
    return pathlib.Path(path).read_text(encoding="utf-8").strip()


def write_toolchain(arguments: list[str]) -> None:
    (
        output, harness, helper, evaluator, budgets, xctrace, xctrace_version,
        xcode_version, developer_directory, host_system, host_kernel,
    ) = arguments
    payload = {}
    for name, path in (
        ("harness", harness), ("helper", helper), ("evaluator", evaluator),
        ("budgets", budgets), ("xctrace", xctrace),
    ):
        payload[name + "Path"] = str(pathlib.Path(path).resolve())
        payload[name + "Sha256"] = sha256(pathlib.Path(path).resolve())
    payload.update({
        "xctraceVersion": log_text(xctrace_version),
        "xcodeVersion": log_text(xcode_version),
        "developerDirectory": log_text(developer_directory),
        "hostSystem": log_text(host_system),
        "hostKernel": log_text(host_kernel),
    })
    write_json(pathlib.Path(output), payload)


def write_host_sample(output: str) -> None:
    completed = subprocess.run(
        ["/bin/ps", "-axo", "pid=,ppid=,%cpu=,command="],
        check=True, capture_output=True, text=True,
    )
    ancestors, current = {os.getpid()}, os.getppid()
    rows = []
    for line in completed.stdout.splitlines():
        parts = line.strip().split(maxsplit=3)
        if len(parts) < 4:
            continue
        try:
            rows.append((int(parts[0]), int(parts[1]), max(0.0, float(parts[2]))))
        except ValueError:
            continue
    parents = {pid: ppid for pid, ppid, _ in rows}
    while current > 1 and current not in ancestors:
        ancestors.add(current)
        current = parents.get(current, 1)
    foreign = [cpu for pid, _, cpu in rows if pid not in ancestors]
    write_json(pathlib.Path(output), {
        "schemaVersion": 1,
        "logicalCPUCount": os.cpu_count() or 1,
        "load1": max(0.0, os.getloadavg()[0]),
        "maxForeignCPUPercent": max(foreign, default=0.0),
        "totalForeignCPUPercent": math.fsum(foreign),
        "sampledProcessCount": len(foreign),
    })


def artifact_ref(root: pathlib.Path, relative: str | None):
    if not relative:
        return None
    path = root / relative
    return {"path": relative, "sha256": sha256(path)} if path.exists() else None


def read_captures(root: pathlib.Path) -> list[dict]:
    captures = []
    path = root / "metadata" / "captures.tsv"
    if not path.exists():
        return captures
    with path.open(encoding="utf-8", newline="") as stream:
        for row in csv.reader(stream, delimiter="\t"):
            (
                slug, run_index, nonce, template, status, trace, toc, kind,
                data, query, marker, host, command,
            ) = row
            captures.append({
                "stageSlug": slug,
                "runIndex": int(run_index),
                "scenarioNonce": nonce,
                "template": template,
                "status": status,
                "trace": artifact_ref(root, trace),
                "tocExport": artifact_ref(root, toc),
                "dataExport": {
                    "kind": kind, "path": data, "sha256": sha256(root / data),
                    "query": query,
                } if data and (root / data).exists() else None,
                "marker": artifact_ref(root, marker),
                "hostSample": artifact_ref(root, host),
                "commandLabel": command or None,
            })
    return captures


def read_commands(root: pathlib.Path) -> list[dict]:
    commands = []
    for command_path in sorted((root / "commands").glob("*.command.txt")):
        base = command_path.name.removesuffix(".command.txt")
        stdout = command_path.with_name(base + ".stdout.log")
        stderr = command_path.with_name(base + ".stderr.log")
        exit_path = command_path.with_name(base + ".exit-code.txt")
        commands.append({
            "label": base,
            "command": artifact_ref(root, str(command_path.relative_to(root))),
            "stdout": artifact_ref(root, str(stdout.relative_to(root))),
            "stderr": artifact_ref(root, str(stderr.relative_to(root))),
            "exitCode": int(exit_path.read_text(encoding="utf-8").strip()),
            "exitCodeReceipt": artifact_ref(root, str(exit_path.relative_to(root))),
        })
    return commands


def write_manifest(arguments: list[str]) -> None:
    (
        root_path, started_at, mode, app_path, device_udid, expected_commit,
        duration, status, exit_code, completed_at,
    ) = arguments
    root = pathlib.Path(root_path)
    errors = [
        line for line in (root / "errors.log").read_text(encoding="utf-8").splitlines()
        if line
    ]
    write_json(root / "manifest.json", {
        "schemaVersion": 2,
        "kind": "mobile-performance-evidence-capture",
        "status": status,
        "exitCode": int(exit_code),
        "startedAt": started_at,
        "completedAt": completed_at,
        "mode": mode,
        "request": {
            "appPath": app_path, "deviceUDID": device_udid,
            "expectedSourceCommit": expected_commit,
            "durationSecondsPerCapture": int(duration),
        },
        "candidate": read_json(root / "metadata" / "candidate.json"),
        "preparationEvidence": artifact_ref(root, "metadata/preparations.tsv"),
        "device": read_json(root / "metadata" / "device.json"),
        "toolchain": read_json(root / "metadata" / "toolchain.json"),
        "captures": read_captures(root),
        "commands": read_commands(root),
        "errors": errors,
        "artifactChecksumIndex": "artifacts.sha256",
        "evaluation": {
            "status": "NOT_EVALUATED",
            "meaning": "Raw capture alone never establishes a performance PASS.",
            "requiredNextStep": "Run the repository evaluator over this immutable capture.",
        },
        "releaseGate": {
            "status": "RED_PENDING_RAW_BUDGET_EVALUATION",
            "blocking": True,
            "acceptedAsFeaturePass": False,
            "meaning": "Raw capture remains blocking until derived budgets and visible E2E pass.",
        },
    })


class XCRows:
    def __init__(self, path: pathlib.Path):
        self.root = ET.parse(path).getroot()
        self.ids = {
            element.attrib["id"]: element for element in self.root.iter()
            if "id" in element.attrib
        }
        schema = self.root.find(".//schema")
        self.columns = [
            column.findtext("mnemonic", "") for column in schema.findall("col")
        ] if schema is not None else []
        self.rows = self.root.findall(".//row")

    def resolve(self, element: ET.Element) -> ET.Element:
        visited = set()
        while "ref" in element.attrib and element.attrib["ref"] not in visited:
            visited.add(element.attrib["ref"])
            element = self.ids.get(element.attrib["ref"], element)
        return element

    def values(self, row: ET.Element) -> dict[str, ET.Element]:
        return {name: child for name, child in zip(self.columns, list(row))}

    def number(self, element: ET.Element | None) -> float | None:
        if element is None:
            return None
        element = self.resolve(element)
        if element.tag == "sentinel" or element.text is None:
            return None
        try:
            value = float(element.text)
            return value if math.isfinite(value) and value >= 0 else None
        except ValueError:
            return None

    def process_pid(self, element: ET.Element | None) -> int | None:
        if element is None:
            return None
        pid = self.resolve(element).find(".//pid")
        value = self.number(pid)
        return int(value) if value is not None else None


def toc_info(path: pathlib.Path, errors: list[str]):
    try:
        root = ET.parse(path).getroot()
        target = root.find(".//run/info/target/process")
        duration = float(root.findtext(".//run/info/summary/duration", ""))
        pid = int(target.attrib["pid"])
        if duration <= 0:
            raise ValueError("non-positive duration")
        return pid, duration
    except (ET.ParseError, OSError, ValueError, TypeError, KeyError) as error:
        errors.append(f"invalid xctrace TOC export {path.name}: {error}")
        return None, None


def add_sample(samples, metric, value, capture, source, detail):
    artifact = capture[source]
    samples.setdefault(metric, []).append({
        "value": value,
        "provenance": {
            "stageSlug": capture["stageSlug"], "runIndex": capture["runIndex"],
            "scenarioNonce": capture["scenarioNonce"],
            "artifact": artifact["path"], "artifactSha256": artifact["sha256"],
            "detail": detail,
        },
    })


def derive_launch(capture, samples, errors):
    pid, _ = toc_info(capture["tocPath"], errors)
    try:
        rows = XCRows(capture["exportPath"])
    except (ET.ParseError, OSError) as error:
        errors.append(f"invalid life-cycle-period export: {error}")
        return
    ends = []
    for index, row in enumerate(rows.rows):
        values = rows.values(row)
        if rows.process_pid(values.get("process")) != pid:
            continue
        start, duration = rows.number(values.get("start")), rows.number(values.get("duration"))
        if start is not None and duration is not None:
            ends.append((start + duration, index))
    if not ends:
        errors.append(f"life-cycle-period export has no rows for target pid {pid}")
        return
    metric = "launch.cold.duration_ms" if capture["stageSlug"] == "launch-cold" else "launch.warm.duration_ms"
    end, row = max(ends)
    add_sample(samples, metric, end / 1e6, capture, "dataExport", f"target-pid={pid};max-end-row={row};nanoseconds")


def derive_sysmon(capture, samples, errors):
    pid, _ = toc_info(capture["tocPath"], errors)
    try:
        rows = XCRows(capture["exportPath"])
    except (ET.ParseError, OSError) as error:
        errors.append(f"invalid sysmon-process export: {error}")
        return
    points = []
    for index, row in enumerate(rows.rows):
        values = rows.values(row)
        if rows.process_pid(values.get("process")) == pid:
            time = rows.number(values.get("time"))
            if time is not None:
                points.append((time, index, values))
    points.sort(key=lambda item: (item[0], item[1]))
    if not points:
        errors.append(f"sysmon-process export has no rows for target pid {pid}")
        return
    slug = capture["stageSlug"]
    memory = {
        "memory-normal-1": "memory.normal.1_tab.resident_mib",
        "memory-normal-5": "memory.normal.5_tabs.resident_mib",
        "memory-normal-20-discard-restore": "memory.normal.20_tabs.resident_mib",
        "memory-private-1": "memory.private.1_tab.resident_mib",
        "memory-private-5": "memory.private.5_tabs.resident_mib",
        "memory-private-20-discard-restore": "memory.private.20_tabs.resident_mib",
    }
    if slug in memory:
        for time, row, values in points:
            resident = rows.number(values.get("memory-resident-size"))
            if resident is not None:
                add_sample(samples, memory[slug], resident / MIB, capture, "dataExport", f"target-pid={pid};row={row};time-ns={int(time)}")
    if slug == "idle-resources":
        for time, row, values in points:
            cpu = rows.number(values.get("cpu-percent"))
            if cpu is not None:
                add_sample(samples, "idle.cpu.percent", cpu, capture, "dataExport", f"target-pid={pid};row={row};time-ns={int(time)}")
        for before, after in zip(points, points[1:]):
            wake_before = rows.number(before[2].get("interrupt-wakeups"))
            wake_after = rows.number(after[2].get("interrupt-wakeups"))
            seconds = (after[0] - before[0]) / 1e9
            if wake_before is not None and wake_after is not None and wake_after >= wake_before and seconds > 0:
                add_sample(samples, "idle.wakeups.per_second", (wake_after - wake_before) / seconds, capture, "dataExport", f"target-pid={pid};rows={before[1]}-{after[1]};delta")


def derive_har(capture, samples, errors):
    _, duration = toc_info(capture["tocPath"], errors)
    try:
        entries = json.loads(capture["exportPath"].read_text(encoding="utf-8"))["log"]["entries"]
        if not isinstance(entries, list):
            raise TypeError("entries is not an array")
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        errors.append(f"invalid HAR export: {error}")
        return
    total = 0.0
    for entry in entries:
        if not isinstance(entry, dict):
            errors.append("HAR contains a malformed entry")
            return
        for side in ("request", "response"):
            record = entry.get(side, {})
            for key in ("headersSize", "bodySize"):
                if is_number(record.get(key)):
                    total += float(record[key])
            if side == "response" and not is_number(record.get("bodySize")):
                content = record.get("content", {}).get("size")
                if is_number(content):
                    total += float(content)
    if duration is None:
        return
    detail, scale = f"entries={len(entries)};toc-seconds={duration};per-minute", 60 / duration
    add_sample(samples, "idle.network.bytes_per_minute", total * scale, capture, "dataExport", detail)
    add_sample(samples, "idle.network.requests_per_minute", len(entries) * scale, capture, "dataExport", detail)


def derive_hitches(capture, samples, errors):
    _, duration = toc_info(capture["tocPath"], errors)
    try:
        rows = XCRows(capture["exportPath"])
    except (ET.ParseError, OSError) as error:
        errors.append(f"invalid hitches-summary export: {error}")
        return
    if duration is None:
        return
    count = max(1, math.ceil(duration))
    occupied, maximum = [0.0] * count, [0.0] * count
    for row in rows.rows:
        values = rows.values(row)
        start_ns, hitch_ns = rows.number(values.get("start")), rows.number(values.get("duration"))
        if start_ns is None or hitch_ns is None:
            continue
        start, end = start_ns / 1e9, (start_ns + hitch_ns) / 1e9
        for bucket in range(max(0, int(start)), min(count, math.ceil(end))):
            occupied[bucket] += max(0.0, min(end, bucket + 1, duration) - max(start, bucket))
            maximum[bucket] = max(maximum[bucket], hitch_ns / 1e6)
    prefix = "animation.reduce_motion" if capture["stageSlug"] == "scroll-motion-reduced" else "animation.standard"
    for bucket in range(count):
        seconds = min(1.0, duration - bucket)
        detail = f"one-second-bucket={bucket};toc-seconds={duration}"
        add_sample(samples, prefix + ".hitch_percent", 100 * occupied[bucket] / seconds, capture, "dataExport", detail)
        add_sample(samples, prefix + ".maximum_hitch_ms", maximum[bucket], capture, "dataExport", detail)


def derive_marker(capture, samples, errors):
    marker = read_json(capture["markerPath"])
    try:
        validate_marker_payload(
            marker, capture["stageSlug"], capture["scenarioNonce"],
            STAGE_CONTRACTS[capture["stageSlug"]][2],
        )
    except ValueError as error:
        errors.append(str(error))
        return
    slug = capture["stageSlug"]
    mapping = {}
    if slug in {"launch-cold", "launch-warm-cache"}:
        mapping = {"sessionReadyMilliseconds": "session.ready.duration_ms"}
    elif slug == "memory-normal-20-discard-restore":
        mapping = {"discardMilliseconds": "discard.normal.duration_ms", "restoreMilliseconds": "restore.normal.duration_ms"}
    elif slug == "memory-private-20-discard-restore":
        mapping = {"discardMilliseconds": "discard.private.duration_ms", "restoreMilliseconds": "restore.private.duration_ms"}
    elif slug == "controller-pressure-policy":
        mapping = {
            "sessionFlushMilliseconds": "session.flush.duration_ms",
            "sessionFlushFailureCounts": "session.flush.failure_count",
            "livePagesForeground": "live_pages.foreground.count",
            "livePagesBackground": "live_pages.background.count",
            "livePagesMemoryWarning": "live_pages.memory_warning.count",
            "backgroundPolicyMilliseconds": "controller.background_policy.duration_ms",
            "foregroundRestoreMilliseconds": "controller.foreground_restore.duration_ms",
        }
    for source, metric in mapping.items():
        for index, value in enumerate(marker["samples"][source]):
            add_sample(samples, metric, float(value), capture, "marker", f"samples.{source}[{index}]")


def derive_samples(captures, errors):
    samples: dict[str, list[dict[str, Any]]] = {}
    for capture in sorted(captures, key=lambda item: (item["stageSlug"], item["runIndex"])):
        kind = capture["dataExport"]["kind"]
        if kind == "life-cycle-period":
            derive_launch(capture, samples, errors)
        elif kind == "sysmon-process":
            derive_sysmon(capture, samples, errors)
        elif kind == "har":
            derive_har(capture, samples, errors)
        elif kind == "hitches-summary":
            derive_hitches(capture, samples, errors)
        derive_marker(capture, samples, errors)
    return samples


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("helper command is required")
    command, arguments = sys.argv[1], sys.argv[2:]
    if command == "validate-plist" and len(arguments) == 1:
        validate_plist(pathlib.Path(arguments[0]))
    elif command == "write-candidate" and len(arguments) == 17:
        write_candidate(arguments)
    elif command == "write-device" and len(arguments) == 5:
        write_device(arguments)
    elif command == "write-toolchain" and len(arguments) == 11:
        write_toolchain(arguments)
    elif command == "write-host-sample" and len(arguments) == 1:
        write_host_sample(arguments[0])
    elif command == "device-kind" and len(arguments) == 1:
        payload = read_json(pathlib.Path(arguments[0]))
        if payload is None:
            raise SystemExit("device metadata is absent or invalid")
        print(payload["kind"])
    elif command == "validate-workload-marker" and len(arguments) == 4:
        validate_workload_marker(arguments)
    elif command == "write-manifest" and len(arguments) == 10:
        write_manifest(arguments)
    else:
        raise SystemExit(f"invalid helper command or argument count: {command}")


if __name__ == "__main__":
    main()
