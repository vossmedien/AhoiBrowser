#!/usr/bin/env python3
"""Capture and verify actual gclient dependency revisions fail-closed."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import re
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
CIPD_VERIFIED_PLATFORM = "mac-arm64"
CIPD_SERVICE_URL = "https://chrome-infra-packages.appspot.com"
CIPD_SERVICE_ROOT = f"{CIPD_SERVICE_URL}/"


def work_root() -> pathlib.Path:
    configured = os.environ.get("AHOI_WORK_ROOT")
    value = pathlib.Path(configured) if configured else ROOT / ".work"
    if not value.is_absolute():
        raise SystemExit("AHOI_WORK_ROOT must be absolute")
    return value.resolve()


def output(*args: str, cwd: pathlib.Path) -> str:
    return subprocess.run(
        args, cwd=cwd, check=True, capture_output=True, text=True
    ).stdout.strip()


def json_hash(value: object) -> str:
    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def collect_revisions(actual: bool) -> dict:
    root = work_root()
    chromium_root = root / "chromium"
    gclient = root / "depot_tools/gclient"
    command = [str(gclient), "revinfo"]
    if actual:
        command.append("--actual")
    command.extend(("--output-json", "-"))
    raw = output(*command, cwd=chromium_root)
    revisions = json.loads(raw)
    if "src" not in revisions:
        raise SystemExit("gclient dependency manifest does not contain src")
    normalized = {}
    for name, entry in sorted(revisions.items()):
        relative = pathlib.PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise SystemExit(f"unsafe gclient dependency path: {name}")
        if not isinstance(entry, dict) or set(entry) != {"url", "rev"}:
            raise SystemExit(f"unexpected gclient revision entry: {name}")
        if entry["url"] is not None and not isinstance(entry["url"], str):
            raise SystemExit(f"unexpected gclient URL for {name}")
        if entry["rev"] is not None and not isinstance(entry["rev"], str):
            raise SystemExit(f"unexpected gclient revision for {name}")
        normalized[name] = {"url": entry["url"], "revision": entry["rev"]}
    if not actual:
        # gclient's JSON formatter splits dependency URLs on every "@" and
        # keeps only element 1. That truncates valid CIPD versions such as
        # "version:2@5.8-release" to the non-existent tag "version:2". The
        # plain revinfo format preserves the complete version. Cross-check the
        # two representations and restore every CIPD version fail-closed.
        raw = output(str(gclient), "revinfo", cwd=chromium_root)
        normalized = restore_expected_cipd_versions(normalized, raw)
    return normalized


def compare_expected_actual(
    expected: dict,
    actual: dict,
    expected_chromium_commit: str,
) -> None:
    if "src" not in actual:
        raise SystemExit("actual gclient manifest does not contain src")
    missing = sorted(expected.keys() - actual.keys())
    if missing:
        raise SystemExit(f"expected gclient dependency is missing: {missing[0]}")
    unexpected = sorted(actual.keys() - expected.keys())
    if unexpected:
        raise SystemExit(
            f"unexpected actual gclient dependency: {unexpected[0]}"
        )
    source_revision = actual["src"]["revision"]
    if source_revision != expected_chromium_commit:
        raise SystemExit(
            "gclient src revision mismatch: "
            f"expected {expected_chromium_commit}, got {source_revision}"
        )
    for name, expected_entry in expected.items():
        actual_entry = actual[name]
        expected_is_gcs = is_gcs_entry(expected_entry)
        actual_is_gcs = is_gcs_entry(actual_entry)
        if expected_is_gcs or actual_is_gcs:
            if not expected_is_gcs or not actual_is_gcs:
                raise SystemExit(f"GCS dependency type mismatch for {name}")
            if actual_entry != expected_entry:
                raise SystemExit(f"GCS dependency closure mismatch for {name}")
            continue
        if ":" in name:
            continue
        expected_revision = expected_entry["revision"]
        actual_revision = actual_entry["revision"]
        if name != "src" and not (
            isinstance(expected_revision, str)
            and re.fullmatch(r"[0-9a-f]{40}", expected_revision)
        ):
            raise SystemExit(f"unpinned Git dependency is not reproducible: {name}")
        if expected_revision is not None and actual_revision is None:
            raise SystemExit(f"dependency revision is missing for {name}")
        if expected_revision is not None and actual_revision != expected_revision:
            raise SystemExit(
                f"dependency closure mismatch for {name}: "
                f"expected {expected_revision}, got {actual_revision}"
            )
        expected_url = expected_entry["url"]
        if expected_url is not None and actual_entry["url"] != expected_url:
            raise SystemExit(f"dependency URL mismatch for {name}")


def is_gcs_entry(entry: dict) -> bool:
    url = entry.get("url")
    return isinstance(url, str) and url.startswith("gs://")


def restore_expected_cipd_versions(revisions: dict, raw: str) -> dict:
    expected_names = {
        name
        for name, entry in revisions.items()
        if ":" in name and not is_gcs_entry(entry)
    }
    recovered: dict[str, str] = {}
    seen_gcs: set[str] = set()
    for line_number, line in enumerate(raw.splitlines(), start=1):
        if not line:
            continue
        if ": " not in line:
            raise SystemExit(
                f"malformed plain gclient revinfo line {line_number}"
            )
        name, raw_url = line.split(": ", 1)
        if ":" not in name:
            continue
        entry = revisions.get(name)
        if entry is None:
            raise SystemExit(f"unexpected plain gclient dependency: {name}")
        if is_gcs_entry(entry):
            if name in seen_gcs:
                raise SystemExit(f"duplicate plain gclient dependency: {name}")
            if raw_url != entry["url"]:
                raise SystemExit(f"plain gclient GCS dependency mismatch: {name}")
            seen_gcs.add(name)
            continue
        if name not in expected_names:
            raise SystemExit(f"unexpected plain gclient CIPD dependency: {name}")
        if name in recovered:
            raise SystemExit(f"duplicate plain gclient dependency: {name}")
        base_url = entry["url"]
        lossy_version = entry["revision"]
        if not isinstance(base_url, str) or not isinstance(lossy_version, str):
            raise SystemExit(f"invalid JSON gclient CIPD dependency: {name}")
        if not base_url.startswith(CIPD_SERVICE_ROOT):
            raise SystemExit(f"unexpected gclient CIPD service for {name}")
        prefix = f"{base_url}@"
        if not raw_url.startswith(prefix):
            raise SystemExit(f"plain gclient CIPD URL mismatch: {name}")
        exact_version = raw_url[len(prefix) :]
        if not exact_version:
            raise SystemExit(f"plain gclient CIPD version is missing: {name}")
        if exact_version != lossy_version and not exact_version.startswith(
            f"{lossy_version}@"
        ):
            raise SystemExit(f"plain gclient CIPD version mismatch: {name}")
        recovered[name] = exact_version

    missing_cipd = sorted(expected_names - recovered.keys())
    if missing_cipd:
        raise SystemExit(
            "plain gclient CIPD dependencies are missing: "
            f"{missing_cipd[:3]}"
        )
    missing_gcs = sorted(
        name
        for name, entry in revisions.items()
        if ":" in name and is_gcs_entry(entry) and name not in seen_gcs
    )
    if missing_gcs:
        raise SystemExit(
            "plain gclient GCS dependencies are missing: "
            f"{missing_gcs[:3]}"
        )

    exact = {name: dict(entry) for name, entry in revisions.items()}
    for name, version in recovered.items():
        exact[name]["revision"] = version
    return exact


def cipd_ensure_file(expected: dict) -> str:
    packages_by_subdir: dict[str, list[tuple[str, str]]] = {}
    for name, entry in expected.items():
        if ":" not in name or is_gcs_entry(entry):
            continue
        subdir, package = name.split(":", 1)
        version = entry["revision"]
        if not isinstance(version, str) or not version:
            raise SystemExit(f"CIPD version is missing for {name}")
        packages_by_subdir.setdefault(subdir, []).append((package, version))

    lines = [
        "$ResolvedVersions /dev/null",
        f"$VerifiedPlatform {CIPD_VERIFIED_PLATFORM}",
    ]
    for subdir, packages in sorted(packages_by_subdir.items()):
        lines.extend(("", f"@Subdir {subdir}"))
        lines.extend(f"{package} {version}" for package, version in sorted(packages))
    lines.append("")
    return "\n".join(lines)


def resolved_cipd_pins(expected: dict) -> set[tuple[str, str, str]]:
    ensure_file = cipd_ensure_file(expected)
    if "@Subdir " not in ensure_file:
        return set()
    with tempfile.TemporaryDirectory(prefix="ahoi-cipd-resolve-") as directory:
        temporary = pathlib.Path(directory)
        ensure_path = temporary / "expected.ensure"
        result_path = temporary / "resolved.json"
        ensure_path.write_text(ensure_file, encoding="utf-8")
        completed = subprocess.run(
            (
                str(work_root() / "depot_tools/cipd"),
                "ensure-file-resolve",
                "-ensure-file",
                str(ensure_path),
                "-json-output",
                str(result_path),
                "-service-url",
                CIPD_SERVICE_URL,
            ),
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            detail = (completed.stderr or completed.stdout).strip()
            raise SystemExit(
                "CIPD expected-version resolution failed"
                + (f": {detail}" if detail else "")
            )
        resolved = json.loads(result_path.read_text(encoding="utf-8")).get(
            "result", {}
        )
    pins = set()
    for subdir, packages in resolved.items():
        for package in packages:
            pin = package.get("pin", {})
            package_name = pin.get("package") or package.get("package")
            instance_id = pin.get("instance_id")
            if not isinstance(package_name, str) or not isinstance(instance_id, str):
                raise SystemExit(f"invalid resolved CIPD pin under {subdir}")
            pins.add((subdir, package_name, instance_id))
    return pins


def actual_cipd_pins(actual: dict) -> set[tuple[str, str, str]]:
    pins = set()
    for name, entry in actual.items():
        if ":" not in name or is_gcs_entry(entry):
            continue
        subdir = name.split(":", 1)[0]
        url = entry["url"]
        if not isinstance(url, str):
            raise SystemExit(f"actual CIPD URL is missing for {name}")
        match = re.fullmatch(
            rf"{re.escape(CIPD_SERVICE_ROOT)}p/(.+)/\+/([^/]+)", url
        )
        if not match:
            raise SystemExit(f"actual CIPD instance URL is malformed for {name}")
        pins.add((subdir, match.group(1), match.group(2)))
    return pins


def verify_cipd_closure(expected: dict, actual: dict) -> None:
    expected_pins = resolved_cipd_pins(expected)
    actual_pins = actual_cipd_pins(actual)
    if expected_pins != actual_pins:
        missing = sorted(expected_pins - actual_pins)
        extra = sorted(actual_pins - expected_pins)
        raise SystemExit(
            "CIPD dependency closure mismatch: "
            f"missing={missing[:3]}, unexpected={extra[:3]}"
        )


def verify_revisions(
    expected: dict,
    actual: dict,
    expected_chromium_commit: str,
    allow_source_overlay: bool,
) -> None:
    compare_expected_actual(expected, actual, expected_chromium_commit)
    verify_cipd_closure(expected, actual)
    root = work_root()
    chromium_root = root / "chromium"
    for name, entry in expected.items():
        checkout_name = name.split(":", 1)[0]
        checkout = chromium_root / pathlib.PurePosixPath(checkout_name)
        if not checkout.exists():
            raise SystemExit(f"expected dependency path is missing: {name}")
        if not (checkout / ".git").exists():
            revision = entry["revision"]
            url = entry["url"]
            if (
                isinstance(revision, str)
                and len(revision) == 40
                and isinstance(url, str)
                and (url.startswith("http") or url.startswith("ssh") or url.startswith("git"))
            ):
                raise SystemExit(f"expected Git checkout metadata is missing: {name}")
            continue
        actual_head = output("git", "rev-parse", "HEAD", cwd=checkout)
        revision = entry["revision"]
        if (
            isinstance(revision, str)
            and len(revision) == 40
            and actual_head != revision
        ):
            raise SystemExit(f"dependency revision mismatch for {name}")
        if name == "src" and allow_source_overlay:
            continue
        dirty = output(
            "git",
            "status",
            "--porcelain",
            "--untracked-files=normal",
            cwd=checkout,
        )
        if dirty:
            raise SystemExit(f"Chromium dependency is dirty: {name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--allow-source-overlay", action="store_true")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    expected = collect_revisions(actual=False)
    actual = collect_revisions(actual=True)
    verify_revisions(
        expected,
        actual,
        expected_chromium_commit=args.expected_commit,
        allow_source_overlay=args.allow_source_overlay,
    )
    payload = {
        "schemaVersion": 1,
        "chromiumCommit": args.expected_commit,
        "sourceOverlayAllowed": args.allow_source_overlay,
        "expectedDependencyCount": len(expected),
        "actualDependencyCount": len(actual),
        "expectedManifestSha256": json_hash(expected),
        "actualManifestSha256": json_hash(actual),
        "verifiedAt": dt.datetime.now(dt.timezone.utc).isoformat(),
        "expectedDependencies": expected,
        "actualDependencies": actual,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
