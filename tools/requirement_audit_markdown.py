"""Markdown presentation for the deterministic requirement audit."""

from __future__ import annotations

from typing import Any


def _markdown_cell(value: object) -> str:
    return str(value).replace("\n", " ").replace("|", "\\|")


def _summary_table(
    title: str, groups: dict[str, dict[str, Any]], label: str
) -> list[str]:
    lines = [
        f"## {title}",
        "",
        f"| {label} | Count | Statuses | IDs |",
        "|---|---:|---|---|",
    ]
    for name, item in groups.items():
        statuses = ", ".join(
            f"{status}={count}" for status, count in item["byStatus"].items()
        )
        ids = ", ".join(f"`{test_id}`" for test_id in item["ids"])
        lines.append(
            f"| {_markdown_cell(name)} | {item['count']} | "
            f"{_markdown_cell(statuses)} | {ids} |"
        )
    lines.append("")
    return lines


def render_markdown(audit: dict[str, Any]) -> str:
    summary = audit["summary"]
    release_chain = audit["releaseChain"]
    lines = [
        "# AhoiBrowser Requirement Audit",
        "",
        f"Registered requirements: **{summary['total']}**  ",
        f"Release chain ready: **{'yes' if release_chain['ready'] else 'no'}**  ",
        f"Evidence root: `{_markdown_cell(audit['evidenceRoot'])}`",
        "",
    ]
    if release_chain["validationErrors"]:
        lines.extend(["Release-chain validation errors:", ""])
        lines.extend(
            f"- {_markdown_cell(error)}"
            for error in release_chain["validationErrors"]
        )
        lines.append("")
    lines.extend(
        _summary_table(
            "Summary by primary class",
            summary["byPrimaryClass"],
            "Primary class",
        )
    )
    lines.extend(_summary_table("Summary by suite", summary["bySuite"], "Suite"))
    lines.extend(["## Summary by status", ""])
    for status, item in summary["byStatus"].items():
        ids = ", ".join(f"`{test_id}`" for test_id in item["ids"])
        lines.append(f"- {status} ({item['count']}): {ids}")
    lines.extend(
        [
            "",
            "## Requirement dispositions",
            "",
            "| ID | Suite | Class | Status | Attempted | Locally controllable | Evidence | External gates | Owner | Condition | Next action |",
            "|---|---|---|---|---|---|---|---|---|---|---|",
        ]
    )
    for item in audit["requirements"]:
        gates = ", ".join(item["externalGateIds"]) or "—"
        evidence_state = item["evidence"]["state"]
        evidence_path = item["evidence"]["path"]
        if evidence_path:
            evidence_state += f" ({evidence_path})"
        lines.append(
            "| "
            + " | ".join(
                _markdown_cell(value)
                for value in (
                    item["id"],
                    item["suite"],
                    item["primaryClass"],
                    item["status"],
                    "yes" if item["attempted"] else "no",
                    "yes" if item["locallyControllable"] else "no",
                    evidence_state,
                    gates,
                    item["owner"],
                    item["condition"],
                    item["nextAction"],
                )
            )
            + " |"
        )
    lines.append("")
    if audit["unmappedEvidence"]:
        lines.extend(["## Unmapped evidence", ""])
        for item in audit["unmappedEvidence"]:
            lines.append(
                f"- `{_markdown_cell(item['path'])}`: "
                f"{_markdown_cell(item['reason'])}"
            )
        lines.append("")
    return "\n".join(lines)
