#!/usr/bin/env python3
"""Compare actual native merge outputs; never launches a runner or a build.

Protocol: fixtures/sync-conformance/RUNNER_OUTPUTS.md. Run receipts are local
owner evidence, not cryptographic attestations that a test process ran.
"""
from __future__ import annotations

import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import sys


class EvidenceError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def _object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _constant(value):
    raise EvidenceError(f"non-finite JSON number: {value}")


def read_json(path):
    raw = Path(path).read_bytes()
    value = json.loads(raw.decode("utf-8"), object_pairs_hook=_object,
                       parse_float=Decimal, parse_constant=_constant)
    canonical(value)  # Also reject unpaired UTF-16 surrogates.
    return raw, value


def canonical(value):
    """Typed, exact JSON value; numbers share a type, strings never normalize."""
    if value is None:
        return ("null",)
    if type(value) is bool:
        return ("bool", value)
    if type(value) in (int, Decimal):
        number = Decimal(value)
        require(number.is_finite(), "non-finite number")
        sign, digits, exponent = number.as_tuple()
        digits = list(digits)
        if not any(digits):
            return ("number", 0, (0,), 0)
        while digits[-1] == 0:
            digits.pop()
            exponent += 1
        return ("number", sign, tuple(digits), exponent)
    if type(value) is str:
        return ("string", value.encode("utf-8"))
    if type(value) is list:
        return ("array", tuple(canonical(item) for item in value))
    require(type(value) is dict, "unsupported JSON value")
    return ("object", tuple(sorted((key.encode("utf-8"), canonical(item))
                                    for key, item in value.items())))


def json_text(value):
    """Lossless JSON for preserved regression inputs, including Decimal tokens."""
    if type(value) is Decimal:
        require(value.is_finite(), "non-finite number")
        return str(value)
    if type(value) is list:
        return "[" + ",".join(json_text(item) for item in value) + "]"
    if type(value) is dict:
        return "{" + ",".join(json.dumps(key) + ":" + json_text(item)
                              for key, item in value.items()) + "}"
    return json.dumps(value, ensure_ascii=True, allow_nan=False)


def differences(left, right, path="$", limit=20):
    if canonical(left) == canonical(right):
        return []
    if type(left) is dict and type(right) is dict:
        result = []
        for key in sorted(left.keys() | right.keys()):
            child = path + "/" + key.replace("~", "~0").replace("/", "~1")
            result.extend([child] if key not in left or key not in right else
                          differences(left[key], right[key], child, limit))
            if len(result) >= limit:
                break
        return result[:limit]
    if type(left) is list and type(right) is list and len(left) == len(right):
        result = []
        for index, (a, b) in enumerate(zip(left, right)):
            result.extend(differences(a, b, f"{path}/{index}", limit))
            if len(result) >= limit:
                break
        return result[:limit]
    return [path]


DECISIONS = {"duplicate", "keepExisting", "acceptIncoming", "mergeFields", "invalid"}


def indexed_cases(document):
    require(type(document) is dict, "document must be an object")
    require(type(document.get("schemaVersion")) is int and document["schemaVersion"] == 1,
            "unsupported schemaVersion")
    cases = document.get("cases")
    require(type(cases) is list and cases, "cases must be a nonempty array")
    result = {}
    for case in cases:
        require(type(case) is dict, "case must be an object")
        name = case.get("name")
        require(type(name) is str and name, "case name missing")
        require(name not in result, f"duplicate case: {name}")
        require(type(case.get("entityType")) is int, f"{name}: entityType must be an integer")
        result[name] = case
    return result


def validate_fixture(document):
    cases = indexed_cases(document)
    for name, case in cases.items():
        expected = case.get("expect")
        require(type(expected) is dict and type(expected.get("decision")) is str and expected["decision"] in DECISIONS,
                f"{name}: invalid expectation")
        require("merged" in expected and
                (expected["merged"] is None if expected["decision"] == "invalid" else
                 type(expected["merged"]) is dict), f"{name}: missing expected payload")
        require("inputValid" not in case or type(case["inputValid"]) is bool,
                f"{name}: inputValid must be boolean")
    return cases


def validate_output(path, receipt_path, implementation, fixture_path, fixture_raw, cases):
    raw, document = read_json(path)
    actual = indexed_cases(document)
    require(document.get("kind") == "ahoi-sync-merge-output", "wrong output kind")
    require(document.get("implementation") == implementation, "wrong output implementation")
    require(document.get("complete") is True, "runner output is incomplete")
    require(document.get("fixtureName") == fixture_path.name, "wrong fixture name")
    require(document.get("fixtureSha256") == sha256(fixture_raw), "wrong fixture hash")
    run_id = document.get("runId")
    require(type(run_id) is str and re.fullmatch(r"[A-Za-z0-9._-]{1,120}", run_id),
            "invalid runId")
    require(actual.keys() == cases.keys(), "output has missing or extra cases")
    for name, case in actual.items():
        require(case["entityType"] == cases[name]["entityType"], f"{name}: wrong entity type")
        require(type(case.get("outcome")) is str and case["outcome"] in {"accepted", "invalid"}, f"{name}: error or skipped case")
        require("payload" in case and "rejectionStage" in case, f"{name}: missing result fields")
        if case["outcome"] == "accepted":
            require(type(case["payload"]) is dict and case["rejectionStage"] is None,
                    f"{name}: accepted result has no canonical payload")
        else:
            require(case["payload"] is None and type(case["rejectionStage"]) is str and case["rejectionStage"] in {"decode", "merge"},
                    f"{name}: invalid rejection result")
        if implementation == "cpp":
            require(type(case.get("decision")) is str and case["decision"] in DECISIONS, f"{name}: missing actual C++ decision")
            require((case["decision"] == "invalid") == (case["outcome"] == "invalid"),
                    f"{name}: contradictory C++ decision")
    _, receipt = read_json(receipt_path)
    require(type(receipt) is dict and type(receipt.get("schemaVersion")) is int and
            receipt["schemaVersion"] == 1 and receipt.get("kind") == "ahoi-sync-merge-run",
            "invalid run receipt")
    for key, expected in (("implementation", implementation), ("runId", run_id),
                          ("fixtureSha256", sha256(fixture_raw)), ("outputSha256", sha256(raw))):
        require(receipt.get(key) == expected, f"receipt {key} mismatch")
    require(receipt.get("completed") is True, "test process did not finish")
    require(type(receipt.get("exitCode")) is int, "missing direct test process exit code")
    require(receipt.get("sourceTreeClean") is True, "source candidate was not frozen/clean")
    require(type(receipt.get("sourceCommit")) is str and
            re.fullmatch(r"[0-9a-f]{40}", receipt["sourceCommit"]), "missing exact source commit")
    binaries = receipt.get("binaryArtifacts")
    require(type(binaries) is dict and binaries, "missing tested binary artifacts")
    for binary, digest in binaries.items():
        require(type(binary) is str and binary and type(digest) is str and
                re.fullmatch(r"[0-9a-f]{64}", digest),
                "invalid binary artifact entry")
        binary_path = Path(receipt_path).parent / binary
        hasher = hashlib.sha256()
        with binary_path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                hasher.update(chunk)
        require(hasher.hexdigest() == digest, f"tested binary hash mismatch: {binary}")
    return actual, receipt


def compare(fixture, cpp, swift, cpp_receipt, swift_receipt):
    fixture = Path(fixture)
    report = {"schemaVersion": 1, "kind": "ahoi-sync-merge-comparison",
              "verdict": "INSUFFICIENT", "differences": []}
    try:
        raw, document = read_json(fixture)
        cases = validate_fixture(document)
        report.update(fixtureName=fixture.name, fixtureSha256=sha256(raw), caseCount=len(cases),
                      entityTypes=sorted({c["entityType"] for c in cases.values()}))
        outputs, receipts = {}, {}
        for impl, path, receipt in (("cpp", cpp, cpp_receipt), ("swift", swift, swift_receipt)):
            outputs[impl], receipts[impl] = validate_output(path, receipt, impl, fixture, raw, cases)
        report["sources"] = {impl: r["sourceCommit"] for impl, r in receipts.items()}
        report["runIds"] = {impl: r["runId"] for impl, r in receipts.items()}
        failures = {impl: r["exitCode"] for impl, r in receipts.items() if r["exitCode"] != 0}
        if failures:
            report["testProcessFailures"] = failures
        for name, case in cases.items():
            expected = case["expect"]
            want = "invalid" if expected["decision"] == "invalid" else "accepted"
            issues = []
            a, b = outputs["cpp"][name], outputs["swift"][name]
            if a["outcome"] != b["outcome"]:
                issues.append("cpp/swift outcome")
            elif a["outcome"] == "accepted":
                issues.extend("cpp/swift payload " + p for p in differences(a["payload"], b["payload"]))
            if a["decision"] != expected["decision"]:
                issues.append("cpp/fixture decision")
            for impl, actual in (("cpp", a), ("swift", b)):
                if actual["outcome"] != want:
                    issues.append(f"{impl}/fixture outcome")
                elif want == "accepted":
                    issues.extend(f"{impl}/fixture payload " + p
                                  for p in differences(actual["payload"], expected["merged"]))
                if case.get("inputValid") and actual["rejectionStage"] == "decode":
                    issues.append(f"{impl}: valid input rejected before merge")
            if issues:
                report["differences"].append({"name": name, "issues": issues})
        if report["differences"]:
            report["verdict"] = "FAIL"
        elif failures:
            report["reason"] = "test process did not exit successfully"
        else:
            report["verdict"] = "PASS"
    except (EvidenceError, OSError, ValueError, UnicodeError) as error:
        report["reason"] = str(error)
    return report


def preserve_failures(fixture, report, directory):
    """Preserve exact failing vectors/seed names; never auto-approve new goldens."""
    require(report["verdict"] == "FAIL", "only observed behavioral differences can be preserved")
    raw, document = read_json(fixture)
    require(sha256(raw) == report["fixtureSha256"], "fixture changed before preservation")
    names = {item["name"] for item in report["differences"]}
    regression = {"schemaVersion": 1, "cases": [case for case in document["cases"]
                                               if case["name"] in names]}
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    (directory / "source-fixture.json").write_bytes(raw)
    (directory / "regression-candidate.json").write_text(json_text(regression) + "\n")
    (directory / "comparison.json").write_text(json_text(report) + "\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("fixture", "cpp", "swift", "cpp-receipt", "swift-receipt"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--preserve-failures", type=Path,
                        help="new directory for failing vectors and unchanged source fixture")
    args = parser.parse_args(argv)
    report = compare(args.fixture, args.cpp, args.swift, args.cpp_receipt, args.swift_receipt)
    if args.preserve_failures and report["verdict"] == "FAIL":
        try:
            preserve_failures(args.fixture, report, args.preserve_failures)
        except (EvidenceError, OSError, ValueError) as error:
            report["preservationError"] = str(error)
            print(json_text(report))
            return 2
    print(json_text(report))
    return {"PASS": 0, "FAIL": 1, "INSUFFICIENT": 2}[report["verdict"]]


if __name__ == "__main__":
    sys.exit(main())
