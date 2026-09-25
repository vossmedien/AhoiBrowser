#!/usr/bin/env python3
"""Compute the engine input key of a desktop build and find reusable candidates.

The key hashes everything that decides the compiled browser: Chromium pin,
overlay and patch stack (the overlay fingerprint), GN arguments, profile,
depot_tools, toolchain identity and dependency build workarounds. The same key
is computed from the repository state and from existing build receipts, so a
matching receipt names a candidate whose rebuild would add nothing.

The key deliberately excludes the repository commit: documentation-only commits
do not change the engine. A reused candidate keeps its own receipt and source
stamp; it is never restamped.

This tool only reads. It never builds, installs or touches the Chromium checkout.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys
from dataclasses import dataclass
from typing import Optional

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from overlay_fingerprint import fingerprint as overlay_fingerprint  # noqa: E402

KEY_SCHEMA = 1
PROFILES = {
    "dev": ("ahoi-dev.gn", "compatible-development"),
    "full-dev": ("ahoi-full-dev.gn", "compatible-development"),
    "release": ("ahoi-release.gn", "pinned-reference"),
    "full-release": ("ahoi-full-release.gn", "pinned-reference"),
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False).encode("utf-8")


def key_of(components: dict) -> str:
    return sha256_bytes(canonical(components))


def load(path: pathlib.Path):
    return json.loads(path.read_text(encoding="utf-8"))


def expected_toolchain(root: pathlib.Path, mode: str) -> dict:
    pins = load(root / "config/toolchain.json")
    # Older pins had no separate compatible-development toolchain; that mode
    # then used the pinned reference.
    if (mode == "compatible-development"
            and "compatibleDevelopment" in pins["xcode"]
            and "compatibleDevelopmentBuild" in pins["sdks"]["macOS"]):
        xcode = pins["xcode"]["compatibleDevelopment"]["build"]
        sdk = pins["sdks"]["macOS"]["compatibleDevelopmentBuild"]
    else:
        xcode = pins["xcode"]["requiredBuild"]
        sdk = pins["sdks"]["macOS"]["chromiumOfficialBuild"]
    return {"mode": mode, "xcodeBuild": xcode, "macOSSDKBuild": sdk}


def workaround_entry(item: dict) -> dict:
    return {"id": item["id"], "patchSha256": item["patchSha256"],
            "targetSha256": item["targetSha256"]}


def repository_workarounds(root: pathlib.Path) -> list[dict]:
    config = load(root / "config/dependency-build-workarounds.json")
    entries = []
    for name, item in config.items():
        if name == "schemaVersion":
            continue
        actual = sha256_bytes((root / item["patchPath"]).read_bytes())
        if actual != item["patchSha256"]:
            raise SystemExit(f"{item['patchPath']}: sha256 {actual} does not match "
                             "config/dependency-build-workarounds.json")
        entries.append(workaround_entry(item))
    return sorted(entries, key=lambda entry: entry["id"])


def repository_components(root: pathlib.Path, profile: str) -> dict:
    gn_file, mode = PROFILES[profile]
    return {
        "schema": KEY_SCHEMA,
        "profile": profile,
        "chromiumCommit": load(root / "config/chromium.json")["commit"],
        "overlayFingerprint": overlay_fingerprint(root),
        "gnArgsSha256": sha256_bytes((root / "config/build" / gn_file).read_bytes()),
        "depotToolsCommit": load(root / "config/depot-tools.json")["commit"],
        "toolchain": expected_toolchain(root, mode),
        "dependencyWorkarounds": repository_workarounds(root),
    }


def receipt_components(receipt: dict) -> Optional[dict]:
    """Return key components of a build receipt, or None if it is not one."""
    try:
        source, toolchain, app = receipt["source"], receipt["toolchain"], receipt["app"]
        components = {
            "schema": KEY_SCHEMA,
            "profile": app["buildProfile"],
            "chromiumCommit": source["chromiumCommit"],
            "overlayFingerprint": source["overlayFingerprint"],
            "gnArgsSha256": receipt["build"]["gnArgsSha256"],
            "depotToolsCommit": source["depotToolsCommit"],
            "toolchain": {"mode": toolchain["mode"],
                          "xcodeBuild": toolchain["xcodeBuild"],
                          "macOSSDKBuild": toolchain["macOSSDKBuild"]},
            "dependencyWorkarounds": sorted(
                (workaround_entry(item) for item in source["dependencyBuildWorkarounds"]),
                key=lambda entry: entry["id"]),
        }
    except (KeyError, TypeError):
        return None
    return components


@dataclass
class Candidate:
    path: pathlib.Path
    key: str
    components: dict
    receipt: dict

    @property
    def usable(self) -> bool:
        return (self.receipt["source"].get("overlayApplied") is True
                and self.receipt["source"].get("repositoryDirty") is False)


def scan_receipts(directory: pathlib.Path) -> list[Candidate]:
    candidates = []
    for path in sorted(directory.rglob("*.json")):
        try:
            receipt = load(path)
        except (OSError, ValueError):
            continue
        if not isinstance(receipt, dict) or not str(receipt.get("kind", "")).startswith("ahoi-"):
            continue
        components = receipt_components(receipt)
        if components is not None:
            candidates.append(Candidate(path, key_of(components), components, receipt))
    return candidates


def installations(directory: pathlib.Path, executable_sha: str) -> list[pathlib.Path]:
    found = []
    for path in sorted(directory.glob("*.json")):
        try:
            receipt = load(path)
        except (OSError, ValueError):
            continue
        if receipt.get("bundle", {}).get("executableSha256") == executable_sha:
            found.append(path)
    return found


def differences(expected: dict, actual: dict) -> list[str]:
    return sorted(name for name in expected if expected[name] != actual.get(name))


def relative(path: pathlib.Path, root: pathlib.Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path)


def lookup(root: pathlib.Path, profile: str, receipts_dir: pathlib.Path,
           install_dir: pathlib.Path) -> dict:
    components = repository_components(root, profile)
    key = key_of(components)
    candidates = scan_receipts(receipts_dir)
    matches = [c for c in candidates if c.key == key]
    result = {
        "engineInputKey": key,
        "components": components,
        "reusable": [],
        "unusableMatches": [],
        "nearest": None,
    }
    for candidate in matches:
        entry = {
            "receipt": relative(candidate.path, root),
            "sourceCommit": candidate.receipt["source"].get("repositoryCommit"),
            "builtAt": candidate.receipt.get("builtAt"),
            "binarySha256": candidate.receipt["app"].get("binarySha256"),
            "installReceipts": [relative(p, root) for p in installations(
                install_dir, candidate.receipt["app"].get("binarySha256"))],
        }
        (result["reusable"] if candidate.usable else result["unusableMatches"]).append(entry)
    if not matches and candidates:
        same_profile = [c for c in candidates if c.components["profile"] == profile] or candidates
        nearest = min(same_profile,
                      key=lambda c: (len(differences(components, c.components)),
                                     str(c.receipt.get("builtAt", ""))[::-1]))
        result["nearest"] = {
            "receipt": relative(nearest.path, root),
            "differs": differences(components, nearest.components),
        }
    return result


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repository", type=pathlib.Path, default=ROOT)
    parser.add_argument("--profile", choices=sorted(PROFILES), default="dev")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("key", help="print the engine input key of the repository state")
    sub.add_parser("components", help="print the key components as JSON")
    receipt = sub.add_parser("receipt", help="print the key of a build receipt")
    receipt.add_argument("path", type=pathlib.Path)
    find = sub.add_parser("lookup", help="find build receipts with the same key")
    find.add_argument("--receipts", type=pathlib.Path, default=None)
    find.add_argument("--installs", type=pathlib.Path, default=None)
    args = parser.parse_args(argv)
    root = args.repository

    if args.command == "key":
        print(key_of(repository_components(root, args.profile)))
    elif args.command == "components":
        print(json.dumps(repository_components(root, args.profile), indent=2, sort_keys=True))
    elif args.command == "receipt":
        components = receipt_components(load(args.path))
        if components is None:
            print(f"{args.path}: not a build receipt", file=sys.stderr)
            return 2
        print(key_of(components))
    else:
        result = lookup(root, args.profile,
                        args.receipts or root / "artifacts/build",
                        args.installs or root / "artifacts/install")
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0 if result["reusable"] else 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
