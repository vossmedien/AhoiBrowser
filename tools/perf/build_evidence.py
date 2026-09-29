"""Bind performance samples to an immutable build receipt and bundle.

No browser, build tool or Chromium checkout is used here. Build configuration
comes from the receipt's Git revision, never from a mutable working tree.
"""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from engine_input_key import key_of, receipt_components  # noqa: E402
from release.common import ReleaseError, tree_sha256  # noqa: E402


class EvidenceError(ValueError):
    pass


# Intentional product differences; optimization, codec and platform options
# remain in the comparison. Unknown differences must not silently compare equal.
PRODUCT_ARGS = {
    "branding_path_component", "branding_path_product", "branding_file_path",
    "enable_compose", "enable_pdf_save_to_drive",
    "disable_fieldtrial_testing_config", "enable_ahoi_ubo_classic",
}
PROFILES = {
    "ahoi-release": "ahoi-release.gn",
    "ahoi-full-release": "ahoi-full-release.gn",
    "unmodified-upstream-control": "upstream-release.gn",
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_file(repository: pathlib.Path, commit: str, path: str) -> bytes:
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise EvidenceError("build receipt lacks a full source revision")
    try:
        return subprocess.check_output(
            ["git", "-C", str(repository), "show", f"{commit}:{path}"],
            stderr=subprocess.PIPE, env={**os.environ, "GIT_NO_LAZY_FETCH": "1"})
    except subprocess.CalledProcessError as error:
        raise EvidenceError(f"receipt source file unavailable: {path}") from error


def gn_values(data: bytes) -> dict:
    """The tracked build profiles contain only literal assignments."""
    result = {}
    for line in data.decode().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        match = re.fullmatch(r"\s*(\w+)\s*=\s*(.+?)\s*", line)
        if not match or match[1] in result:
            raise EvidenceError("unsupported or duplicate GN assignment")
        try:
            result[match[1]] = json.loads(match[2])
        except ValueError as error:
            raise EvidenceError("GN profile must contain literal values") from error
    return result


def optimization_problem(value: dict | None) -> str | None:
    """Effective flags/profile must be captured by the build owner.

    Merely naming a release GN file does not prove the effective defaults or
    the PGO file actually supplied to the compiler.
    """
    if not isinstance(value, dict):
        return "missing effective optimization evidence in build receipt"
    if type(value.get("chromePgoPhase")) is not int or value["chromePgoPhase"] != 2:
        return "release measurement requires optimization with pinned PGO (phase 2)"
    if type(value.get("useThinLto")) is not bool:
        return "missing effective ThinLTO setting"
    profile = value.get("pgoProfile", {})
    if not isinstance(profile, dict):
        return "missing pinned PGO profile evidence"
    if (profile.get("target") != "mac-arm"
            or profile.get("matchesChromiumPin") is not True
            or not isinstance(profile.get("name"), str)
            or not re.fullmatch(r"[^/\\]+\.profdata", profile["name"])
            or not re.fullmatch(r"[0-9a-f]{64}", str(profile.get("sha256", "")))):
        return "missing pinned PGO profile evidence"
    return None


def verify(identity: dict, receipt_path: pathlib.Path,
           repository: pathlib.Path = ROOT) -> dict:
    raw = receipt_path.read_bytes()
    receipt = json.loads(raw)
    try:
        kind = receipt["kind"]
        if receipt["schemaVersion"] != 2 or kind not in PROFILES:
            raise EvidenceError("a release build receipt is required")
        app, source, build, toolchain = (
            receipt["app"], receipt["source"], receipt["build"], receipt["toolchain"])
        upstream = kind == "unmodified-upstream-control"
        if source["repositoryDirty"] is not False or source["overlayApplied"] is not (not upstream):
            raise EvidenceError("receipt source is dirty or has the wrong overlay state")
        commit = source["repositoryCommit"]
        args = source_file(repository, commit, "config/build/" + PROFILES[kind])
        if sha(args) != build["gnArgsSha256"] or sha(args) != build["generatedGnArgsSha256"]:
            raise EvidenceError("configured/generated GN arguments do not match the source receipt")
        values = gn_values(args)
        required = {"target_os": "mac", "target_cpu": "arm64", "is_debug": False,
                    "is_component_build": False, "is_official_build": True}
        if any(type(values.get(k)) is not type(v) or values[k] != v
               for k, v in required.items()):
            raise EvidenceError("component, debug or non-release GN configuration")
        pin = json.loads(source_file(repository, commit, "config/chromium.json"))
        if source["chromiumCommit"] != pin["commit"] or source["chromiumVersion"] != pin["version"]:
            raise EvidenceError("receipt Chromium identity differs from its source pin")
        if identity["chromiumVersion"] != source["chromiumVersion"]:
            raise EvidenceError("bundle Chromium version differs from receipt")
        for name in ("binarySha256", "bundleIdentifier"):
            if not identity.get(name) or identity[name] != app[name]:
                raise EvidenceError(f"bundle {name} differs from receipt")
        bundle_tree = tree_sha256(pathlib.Path(identity["path"]))
        if bundle_tree != app["bundleTreeSha256"]:
            raise EvidenceError("bundle tree differs from receipt (including framework/resources)")
        if upstream:
            if app["bundleIdentifier"] != "org.chromium.Chromium":
                raise EvidenceError("baseline is not unmodified Chromium")
        else:
            profile = kind.removeprefix("ahoi-")
            for name, expected in (("sourceCommit", commit), ("buildProfile", profile),
                                   ("chromiumCommit", pin["commit"]),
                                   ("gnArgsSha256", sha(args))):
                if identity.get(name) != expected or app.get(name) != expected:
                    raise EvidenceError(f"Ahoi bundle {name} differs from receipt")
            components = receipt_components(receipt)
            if components is None or receipt.get("engineInputKey") != key_of(components):
                raise EvidenceError("missing or inconsistent engine input key")
        pins = json.loads(source_file(repository, commit, "config/toolchain.json"))
        expected_tools = {
            "mode": "pinned-reference", "xcodeVersion": pins["xcode"]["requiredVersion"],
            "xcodeBuild": pins["xcode"]["requiredBuild"],
            "macOSSDKBuild": pins["sdks"]["macOS"]["chromiumOfficialBuild"],
        }
        if not str(expected_tools["xcodeVersion"]).startswith("27."):
            raise EvidenceError("H3 reference requires Xcode 27")
        if any(toolchain.get(k) != v for k, v in expected_tools.items()):
            raise EvidenceError("receipt toolchain differs from pinned Xcode/SDK")
        compiler = {name: build[name]["binarySha256"] for name in ("clang", "lld")}
        for name, digest in compiler.items():
            if digest != pins["buildTools"][name + "BinarySha256"]:
                raise EvidenceError(f"receipt {name} differs from its trusted pin")
        optimization = build.get("effectiveOptimization")
        problem = optimization_problem(optimization)
        return {
            "schemaVersion": 1, "verified": True, "kind": kind,
            "receiptPath": str(receipt_path.resolve()), "receiptSha256": sha(raw),
            "sourceCommit": commit, "binarySha256": identity["binarySha256"],
            "bundleTreeSha256": bundle_tree, "engineInputKey": receipt.get("engineInputKey"),
            "budgetEligible": problem is None, "reason": problem,
            "comparison": {"chromiumCommit": pin["commit"],
                           "gnArguments": {k: v for k, v in values.items() if k not in PRODUCT_ARGS},
                           "toolchain": expected_tools, "compiler": compiler,
                           "optimization": optimization},
        }
    except (KeyError, TypeError, AttributeError) as error:
        raise EvidenceError("incomplete or malformed build receipt") from error
    except ReleaseError as error:
        raise EvidenceError("bundle tree is unavailable or invalid") from error


def budget_problem(run: dict, baseline: bool = False) -> str | None:
    proof = run.get("buildEvidence")
    if not isinstance(proof, dict) or proof.get("verified") is not True:
        return "missing verified build receipt and bundle binding"
    if proof.get("budgetEligible") is not True:
        return proof.get("reason") or "build is not eligible for release budgets"
    expected = {"unmodified-upstream-control"} if baseline else {"ahoi-release", "ahoi-full-release"}
    if proof.get("kind") not in expected:
        return "release candidate / unmodified release baseline roles do not match"
    if proof.get("binarySha256") != run.get("app", {}).get("binarySha256"):
        return "run binary differs from its build evidence"
    if not proof.get("comparison"):
        return "missing build comparison configuration"
    return optimization_problem(proof["comparison"].get("optimization"))
