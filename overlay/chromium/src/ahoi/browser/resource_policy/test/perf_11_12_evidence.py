#!/usr/bin/env python3
"""Evaluates two Ahoi ResourcePolicy snapshots against PERF-11/12."""

import argparse
import json
import pathlib
import sys
from typing import Any


PROTECTED_REASONS = frozenset(
    {
        "active-pane",
        "visible-pane",
        "audible",
        "recently-audible",
        "media-session",
        "picture-in-picture",
        "capture",
        "download",
        "upload",
        "unsaved-form",
        "before-unload",
        "devtools",
        "http-auth",
        "permission-prompt",
        "file-chooser",
        "modal-flow",
        "product-protection",
        "never-sleep",
        "enterprise-policy",
    }
)


def _tabs_by_handle(snapshot: dict[str, Any]) -> dict[int, dict[str, Any]]:
    result: dict[int, dict[str, Any]] = {}
    for tab in snapshot.get("tabs", []):
        handle = tab.get("tab_handle")
        if not isinstance(handle, int) or handle in result:
            raise ValueError("tab_handle values must be unique integers")
        result[handle] = tab
    return result


def evaluate(
    before: dict[str, Any], after: dict[str, Any], minimum_tabs: int = 100
) -> dict[str, Any]:
    """Returns a machine-readable PERF-11/12 report."""
    before_tabs = _tabs_by_handle(before)
    after_tabs = _tabs_by_handle(after)
    checks: dict[str, dict[str, Any]] = {}

    def record(name: str, passed: bool, detail: str) -> None:
        checks[name] = {"passed": passed, "detail": detail}

    record(
        "minimum_tab_count",
        len(before_tabs) >= minimum_tabs,
        f"observed {len(before_tabs)} tabs; required at least {minimum_tabs}",
    )
    missing = sorted(set(before_tabs) - set(after_tabs))
    added = sorted(set(after_tabs) - set(before_tabs))
    record(
        "tab_identity_retained",
        not missing and not added,
        f"missing={missing}, added={added}",
    )

    changed_urls: list[int] = []
    changed_history: list[int] = []
    sleeping: list[int] = []
    protected_sleeping: list[int] = []
    for handle in sorted(set(before_tabs) & set(after_tabs)):
        old = before_tabs[handle]
        new = after_tabs[handle]
        if old.get("url") != new.get("url"):
            changed_urls.append(handle)
        if old.get("navigation_entry_count") != new.get(
            "navigation_entry_count"
        ):
            changed_history.append(handle)
        if old.get("state") != "sleeping" and new.get("state") == "sleeping":
            sleeping.append(handle)
        reasons = {old.get("block_reason"), new.get("block_reason")}
        if reasons & PROTECTED_REASONS and new.get("state") == "sleeping":
            protected_sleeping.append(handle)

    record("url_retained", not changed_urls, f"changed handles={changed_urls}")
    record(
        "navigation_history_retained",
        not changed_history,
        f"changed handles={changed_history}",
    )
    record(
        "eligible_tabs_slept",
        bool(sleeping),
        f"newly sleeping handles={sleeping}",
    )
    record(
        "critical_tabs_protected",
        not protected_sleeping,
        f"protected sleeping handles={protected_sleeping}",
    )

    before_rss = before.get("browser_tree_rss_kib")
    after_rss = after.get("browser_tree_rss_kib")
    rss_valid = isinstance(before_rss, int) and isinstance(after_rss, int)
    record(
        "rss_decreased",
        rss_valid and after_rss < before_rss,
        f"before={before_rss} KiB, after={after_rss} KiB",
    )
    memory_saver_enabled = bool(after.get("memory_saver_enabled"))
    record(
        "memory_saver_enabled",
        memory_saver_enabled,
        f"after={memory_saver_enabled}",
    )

    return {
        "schema_version": 1,
        "passed": all(check["passed"] for check in checks.values()),
        "checks": checks,
        "summary": {
            "tab_count": len(before_tabs),
            "newly_sleeping_count": len(sleeping),
            "rss_reclaimed_kib":
                before_rss - after_rss if rss_valid else None,
        },
    }


def _read_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--before", type=pathlib.Path, required=True)
    parser.add_argument("--after", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--minimum-tabs", type=int, default=100)
    args = parser.parse_args()

    report = evaluate(
        _read_json(args.before),
        _read_json(args.after),
        minimum_tabs=args.minimum_tabs,
    )
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    else:
        sys.stdout.write(encoded)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
