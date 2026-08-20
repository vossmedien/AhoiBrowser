#!/usr/bin/env python3
"""Record fail-closed, privacy-safe provenance for an AhoiBrowser build."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import plistlib
import subprocess
from typing import Optional

from chromium_dependencies import collect_revisions, verify_revisions
from evidence import bundle_hash
from overlay_state import OverlayStateError, verify_overlay_state


ROOT = pathlib.Path(__file__).resolve().parents[1]


def configured_work_root() -> pathlib.Path:
    raw = os.environ.get("AHOI_WORK_ROOT")
    work_root = pathlib.Path(raw) if raw else ROOT / ".work"
    if not work_root.is_absolute():
        raise SystemExit("AHOI_WORK_ROOT must be absolute")
    return work_root.resolve()


WORK_ROOT = configured_work_root()
CHROMIUM_ROOT = WORK_ROOT / "chromium"
CHROMIUM_SRC = CHROMIUM_ROOT / "src"
DEPOT_TOOLS = WORK_ROOT / "depot_tools"


def output(*args: str, cwd: Optional[pathlib.Path] = None) -> str:
    return subprocess.run(
        args,
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def json_sha256(value: object) -> str:
    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def load_json(path: pathlib.Path):
    return json.loads(path.read_text(encoding="utf-8"))


def logical_path(path: pathlib.Path) -> str:
    resolved = path.resolve()
    for root, label in ((ROOT.resolve(), "<repo>"), (WORK_ROOT, "<work-root>")):
        try:
            relative = resolved.relative_to(root)
        except ValueError:
            continue
        return label if str(relative) == "." else f"{label}/{relative.as_posix()}"
    raise SystemExit(f"refusing to publish an absolute local path: {resolved}")


def parse_xcode_version(value: str) -> tuple[str, str]:
    lines = value.splitlines()
    if len(lines) != 2 or not lines[0].startswith("Xcode ") or not lines[1].startswith(
        "Build version "
    ):
        raise SystemExit(f"unexpected xcodebuild -version output: {value!r}")
    return lines[0].split(" ", 1)[1], lines[1].split(" ", 2)[2]


def verify_deps_pins(deps_text: str, pins: dict) -> None:
    expected = {
        "GN": f"'gn_version': 'git_revision:{pins['gnRevision']}'",
        "Ninja": f"'ninja_version': '{pins['ninjaVersion']}'",
        "Siso": f"'siso_version': 'git_revision:{pins['sisoRevision']}'",
        "Clang": f"Mac_arm64/clang-{pins['clangPackage']}.tar.xz",
        "Clang archive SHA-256": pins["clangArchiveSha256"],
    }
    missing = [name for name, marker in expected.items() if marker not in deps_text]
    if missing:
        raise SystemExit(
            "Chromium DEPS does not contain configured pins: " + ", ".join(missing)
        )


def verify_build_tool_identity(
    name: str,
    actual_version: str,
    actual_sha256: str,
    expected_version: str,
    expected_sha256: str,
) -> None:
    if actual_version != expected_version:
        raise SystemExit(
            f"{name} version mismatch: expected {expected_version!r}, "
            f"got {actual_version!r}"
        )
    if actual_sha256 != expected_sha256:
        raise SystemExit(f"{name} binary SHA-256 does not match the trusted pin")


def verify_profile_binding(
    kind: str,
    app: pathlib.Path,
    out_dir: pathlib.Path,
    gn_args: pathlib.Path,
    plist: dict,
) -> None:
    profiles = {
        "upstream": ("upstream-release.gn", "AhoiUpstreamRelease", "Chromium.app"),
        "dev": ("ahoi-dev.gn", "AhoiDev", "AhoiBrowser.app"),
        "release": ("ahoi-release.gn", "AhoiRelease", "AhoiBrowser.app"),
    }
    args_name, out_name, app_name = profiles[kind]
    expected_args = (ROOT / "config/build" / args_name).resolve()
    expected_out = (CHROMIUM_SRC / "out" / out_name).resolve()
    expected_app = (expected_out / app_name).resolve()
    if gn_args != expected_args:
        raise SystemExit(f"{kind} provenance requires {expected_args}")
    if out_dir != expected_out:
        raise SystemExit(f"{kind} provenance requires output directory {expected_out}")
    if app != expected_app:
        raise SystemExit(f"{kind} provenance requires app bundle {expected_app}")
    if kind != "upstream":
        if plist.get("AhoiBuildProfile") != kind:
            raise SystemExit("AhoiBuildProfile does not match provenance kind")
        if plist.get("AhoiGNArgsSHA256") != sha256(gn_args):
            raise SystemExit("Ahoi GN stamp does not match the bound build profile")


def expected_xcode_for_kind(kind: str, toolchain: dict) -> dict[str, str]:
    ios_sdk = toolchain["sdks"]["iOS"]
    if kind == "dev":
        compatible = toolchain["xcode"]["compatibleDevelopment"]
        return {
            "mode": "compatible-development",
            "version": compatible["version"],
            "build": compatible["build"],
            "developerDirectory": compatible["developerDirectory"],
            "iOSSDKBuild": ios_sdk["compatibleDevelopmentBuild"],
        }
    if kind not in {"upstream", "release"}:
        raise SystemExit(f"unsupported build provenance kind: {kind}")
    return {
        "mode": "pinned-reference",
        "version": toolchain["xcode"]["requiredVersion"],
        "build": toolchain["xcode"]["requiredBuild"],
        "developerDirectory": toolchain["xcode"]["developerDirectory"],
        "iOSSDKBuild": ios_sdk["pinnedReferenceBuild"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--kind", choices=("upstream", "dev", "release"), required=True
    )
    parser.add_argument("--app", type=pathlib.Path, required=True)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    parser.add_argument("--gn-args", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    app = args.app.resolve()
    out_dir = args.out_dir.resolve()
    gn_args = args.gn_args.resolve()
    is_ahoi = args.kind != "upstream"
    plist_path = app / "Contents/Info.plist"
    with plist_path.open("rb") as handle:
        plist = plistlib.load(handle)
    verify_profile_binding(args.kind, app, out_dir, gn_args, plist)
    executable = app / "Contents/MacOS" / plist["CFBundleExecutable"]
    chromium_pin = load_json(ROOT / "config/chromium.json")
    toolchain = load_json(ROOT / "config/toolchain.json")
    if output("git", "status", "--porcelain", cwd=ROOT):
        raise SystemExit("refusing provenance for a dirty Ahoi repository")
    gclient_path = CHROMIUM_ROOT / ".gclient"
    canonical_gclient = ROOT / "config/gclient.py"
    if gclient_path.read_bytes() != canonical_gclient.read_bytes():
        raise SystemExit("Chromium .gclient differs from the canonical config")

    actual_chromium_commit = output("git", "rev-parse", "HEAD", cwd=CHROMIUM_SRC)
    if actual_chromium_commit != chromium_pin["commit"]:
        raise SystemExit("Chromium checkout does not match the configured commit")
    overlay_verification = None
    if is_ahoi:
        state_path = WORK_ROOT / "state" / (
            "overlay-" + chromium_pin["commit"] + ".json"
        )
        try:
            overlay_verification = verify_overlay_state(
                ROOT,
                CHROMIUM_SRC,
                state_path,
                chromium_pin["commit"],
            )
        except (OSError, OverlayStateError, SystemExit) as error:
            raise SystemExit(f"overlay verification failed: {error}") from error
    depot_commit = output("git", "rev-parse", "HEAD", cwd=DEPOT_TOOLS)
    depot_pin = load_json(ROOT / "config/depot-tools.json")
    if depot_commit != depot_pin["commit"]:
        raise SystemExit("depot_tools checkout does not match the configured commit")

    deps_path = CHROMIUM_SRC / "DEPS"
    deps_text = deps_path.read_text(encoding="utf-8")
    verify_deps_pins(deps_text, toolchain["buildTools"])
    expected_revisions = collect_revisions(actual=False)
    actual_revisions = collect_revisions(actual=True)
    verify_revisions(
        expected_revisions,
        actual_revisions,
        expected_chromium_commit=chromium_pin["commit"],
        allow_source_overlay=is_ahoi,
    )

    gn_binary = CHROMIUM_SRC / "buildtools/mac/gn"
    ninja_binary = CHROMIUM_SRC / "third_party/ninja/ninja"
    clang_binary = CHROMIUM_SRC / "third_party/llvm-build/Release+Asserts/bin/clang"
    lld_binary = CHROMIUM_SRC / "third_party/llvm-build/Release+Asserts/bin/lld"
    ld64_lld = CHROMIUM_SRC / "third_party/llvm-build/Release+Asserts/bin/ld64.lld"
    for name, binary in (
        ("GN", gn_binary),
        ("Ninja", ninja_binary),
        ("Clang", clang_binary),
        ("LLD", lld_binary),
    ):
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise SystemExit(f"pinned {name} binary is missing or not executable: {binary}")
    gn_version = output(str(gn_binary), "--version", cwd=CHROMIUM_SRC)
    ninja_version = output(str(ninja_binary), "--version", cwd=CHROMIUM_SRC)
    clang_version = output(str(clang_binary), "--version", cwd=CHROMIUM_SRC)
    lld_version = output(str(ld64_lld), "--version", cwd=CHROMIUM_SRC)
    pins = toolchain["buildTools"]
    verify_build_tool_identity(
        "GN",
        gn_version,
        sha256(gn_binary),
        pins["gnVersionOutput"],
        pins["gnBinarySha256"],
    )
    verify_build_tool_identity(
        "Ninja",
        ninja_version,
        sha256(ninja_binary),
        pins["ninjaVersionOutput"],
        pins["ninjaBinarySha256"],
    )
    clang_version_line = clang_version.splitlines()[0] if clang_version else ""
    verify_build_tool_identity(
        "Clang",
        clang_version_line,
        sha256(clang_binary),
        pins["clangVersionLine"],
        pins["clangBinarySha256"],
    )
    if not ld64_lld.is_symlink() or os.readlink(ld64_lld) != "lld":
        raise SystemExit("ld64.lld must be the pinned symlink to lld")
    lld_version_line = lld_version.splitlines()[0] if lld_version else ""
    verify_build_tool_identity(
        "LLD",
        lld_version_line,
        sha256(lld_binary),
        pins["lldVersionLine"],
        pins["lldBinarySha256"],
    )
    gn_args_text = gn_args.read_text(encoding="utf-8")
    if "use_siso = false" not in gn_args_text:
        raise SystemExit("provenance only supports the configured Ninja build path")
    generated_args = out_dir / "args.gn"
    if not generated_args.is_file():
        raise SystemExit("generated GN args are missing from the output directory")
    if generated_args.read_text(encoding="utf-8").strip() != gn_args_text.strip():
        raise SystemExit("generated GN args differ from the configured build profile")

    xcode_output = output("xcodebuild", "-version")
    xcode_version, xcode_build = parse_xcode_version(xcode_output)
    expected_xcode = expected_xcode_for_kind(args.kind, toolchain)
    if xcode_version != expected_xcode["version"]:
        raise SystemExit("Xcode version does not match the configured toolchain")
    if xcode_build != expected_xcode["build"]:
        raise SystemExit("Xcode build does not match the configured toolchain")
    selected_developer_directory = os.environ.get("DEVELOPER_DIR")
    if not selected_developer_directory:
        raise SystemExit("DEVELOPER_DIR must select the configured Xcode explicitly")
    if pathlib.Path(selected_developer_directory).resolve() != pathlib.Path(
        expected_xcode["developerDirectory"]
    ).resolve():
        raise SystemExit("DEVELOPER_DIR does not match the configured toolchain mode")
    macos_sdk_version = output("xcrun", "--sdk", "macosx", "--show-sdk-version")
    if macos_sdk_version != toolchain["sdks"]["macOS"]["testedVersion"]:
        raise SystemExit("macOS SDK version does not match the configured toolchain")
    ios_sdk_version = output("xcrun", "--sdk", "iphoneos", "--show-sdk-version")
    ios_sdk_build = output(
        "xcrun", "--sdk", "iphoneos", "--show-sdk-build-version"
    )
    if ios_sdk_version != toolchain["sdks"]["iOS"]["testedVersion"]:
        raise SystemExit("iOS SDK version does not match the configured toolchain")
    if ios_sdk_build != expected_xcode["iOSSDKBuild"]:
        raise SystemExit("iOS SDK build does not match the configured toolchain")
    sdk_path = pathlib.Path(output("xcrun", "--sdk", "macosx", "--show-sdk-path"))
    sdk_system_version = sdk_path / "System/Library/CoreServices/SystemVersion.plist"
    with sdk_system_version.open("rb") as handle:
        sdk_metadata = plistlib.load(handle)
    macos_sdk_build = sdk_metadata["ProductBuildVersion"]
    if macos_sdk_build != toolchain["sdks"]["macOS"]["chromiumOfficialBuild"]:
        raise SystemExit("macOS SDK build does not match the Chromium baseline")

    app_payload = {
        "path": logical_path(app),
        "bundleSha256": bundle_hash(app),
        "binarySha256": sha256(executable),
        "bundleName": plist["CFBundleName"],
        "bundleIdentifier": plist["CFBundleIdentifier"],
        "marketingVersion": plist["CFBundleShortVersionString"],
        "buildNumber": plist["CFBundleVersion"],
    }
    if is_ahoi:
        app_payload.update(
            {
                "productVersion": plist["AhoiProductVersion"],
                "channel": plist["AhoiUpdateChannel"],
                "sourceCommit": plist["AhoiSourceCommit"],
                "chromiumVersion": plist["AhoiChromiumVersion"],
                "chromiumCommit": plist["AhoiChromiumCommit"],
                "gnArgsSha256": plist["AhoiGNArgsSHA256"],
                "buildProfile": plist["AhoiBuildProfile"],
            }
        )
    else:
        if app_payload["bundleName"] != "Chromium":
            raise SystemExit("upstream control app is not Chromium")
        if app_payload["bundleIdentifier"] != "org.chromium.Chromium":
            raise SystemExit("upstream control bundle ID is not org.chromium.Chromium")

    source_payload = {
        "repositoryCommit": output("git", "rev-parse", "HEAD", cwd=ROOT),
        "repositoryDirty": False,
        "chromiumCommit": actual_chromium_commit,
        "chromiumVersion": chromium_pin["version"],
        "chromiumDepsSha256": sha256(deps_path),
        "depotToolsCommit": depot_commit,
        "gclientConfigSha256": sha256(canonical_gclient),
        "overlayApplied": is_ahoi,
        "expectedDependencyCount": len(expected_revisions),
        "actualDependencyCount": len(actual_revisions),
        "expectedDependencyManifestSha256": json_sha256(expected_revisions),
        "actualDependencyManifestSha256": json_sha256(actual_revisions),
        "expectedDependencyRevisions": expected_revisions,
        "actualDependencyRevisions": actual_revisions,
    }
    if overlay_verification is not None:
        source_payload.update(
            {
                "overlayFingerprint": overlay_verification.input_fingerprint,
                "checkoutDeltaFingerprint": (
                    overlay_verification.checkout_delta_fingerprint
                ),
            }
        )

    payload = {
        "schemaVersion": 2,
        "kind": (
            f"ahoi-{args.kind}"
            if is_ahoi
            else "unmodified-upstream-control"
        ),
        "builtAt": dt.datetime.now(dt.timezone.utc).isoformat(),
        "app": app_payload,
        "source": source_payload,
        "build": {
            "outDirectory": logical_path(out_dir),
            "gnArgsPath": logical_path(gn_args),
            "gnArgsSha256": sha256(gn_args),
            "generatedGnArgsPath": logical_path(generated_args),
            "generatedGnArgsSha256": sha256(generated_args),
            "gn": {"version": gn_version, "binarySha256": pins["gnBinarySha256"]},
            "ninja": {
                "version": ninja_version,
                "binarySha256": pins["ninjaBinarySha256"],
            },
            "siso": {
                "enabled": False,
                "configuredRevision": toolchain["buildTools"]["sisoRevision"],
            },
            "clang": {
                "version": clang_version_line,
                "package": toolchain["buildTools"]["clangPackage"],
                "archiveSha256": pins["clangArchiveSha256"],
                "binarySha256": pins["clangBinarySha256"],
            },
            "lld": {
                "version": lld_version_line,
                "binarySha256": pins["lldBinarySha256"],
                "driver": "ld64.lld -> lld",
            },
        },
        "toolchain": {
            "mode": expected_xcode["mode"],
            "developerDirectory": expected_xcode["developerDirectory"],
            "xcodeVersion": xcode_version,
            "xcodeBuild": xcode_build,
            "macOSSDKVersion": macos_sdk_version,
            "macOSSDKBuild": macos_sdk_build,
            "iOSSDKVersion": ios_sdk_version,
            "iOSSDKBuild": ios_sdk_build,
            "pins": toolchain,
        },
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
