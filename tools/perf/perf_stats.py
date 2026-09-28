"""Statistics and budget evaluation for AhoiBrowser performance runs.

A run file (`kind: ahoi-perf-run`) holds raw samples per metric. `evaluate`
compares a candidate run with a baseline run of unmodified Chromium from the
same revision and host and applies the Master budgets (PERF-01..07). Verdicts
are PASS, FAIL, INSUFFICIENT (too few or too noisy samples, or mismatched
conditions) and NOT_MEASURED. INSUFFICIENT is never a pass.
"""

from __future__ import annotations

import math
import random
import statistics
from typing import Optional

import build_evidence

MIN_SAMPLES = 5
MAX_RELATIVE_SPREAD = 0.10  # MAD / median
BOOTSTRAP_ROUNDS = 2000

# direction: which way is better. kind "relative": allowed worsening of the
# candidate median versus baseline median as a fraction. kind "absolute": the
# candidate statistic must stay below the limit (no baseline needed).
BUDGETS = {
    "PERF-01": {"metric": "speedometer_score", "direction": "higher", "kind": "relative",
                "limit": 0.03, "text": "Speedometer regression at most 3 %"},
    "PERF-02": {"metric": "startup_warm_ms", "direction": "lower", "kind": "relative",
                "limit": 0.10, "text": "start time at most 10 % worse"},
    "PERF-02-first": {"metric": "startup_first_launch_ms", "direction": "lower",
                      "kind": "relative", "limit": 0.10,
                      "text": "fresh-profile first launch at most 10 % worse"},
    "PERF-03": {"metric": "command_bar_ms", "direction": "lower", "kind": "absolute",
                "statistic": "p95", "limit": 50.0, "text": "command bar p95 below 50 ms"},
    "PERF-04": {"metric": "workspace_switch_ms", "direction": "lower", "kind": "absolute",
                "statistic": "p95", "limit": 100.0,
                "text": "workspace switch commit below 100 ms"},
    "PERF-04-presented": {"metric": "workspace_switch_presented_ms", "direction": "lower",
                          "kind": "absolute", "statistic": "p95", "limit": 100.0,
                          "text": "first presented frame after a workspace switch below 100 ms"},
    "PERF-06": {"metric": "memory_20_tabs_kib", "direction": "lower", "kind": "relative",
                "limit": 0.05, "text": "own memory overhead with 20 tabs at most 5 %"},
    "PERF-07": {"metric": "idle_cpu_percent", "direction": "lower", "kind": "idle",
                "limit": 0.1, "text": "no measurable own idle CPU"},
}

# Conditions that must be identical between candidate and baseline runs.
MATCHED_CONDITIONS = ("validationRun", "chromiumVersion", "hardwareModel", "osBuild", "powerSource",
                      "flags", "windowSize", "accessibilityClients", "scenarioVersion")


def percentile(values: list[float], fraction: float) -> float:
    """Nearest-rank percentile, the conservative choice for small n."""
    ordered = sorted(values)
    rank = max(1, math.ceil(fraction * len(ordered)))
    return ordered[rank - 1]


def mad(values: list[float]) -> float:
    center = statistics.median(values)
    return statistics.median(abs(value - center) for value in values)


def summarize(values: list[float]) -> dict:
    if not values:
        return {"n": 0}
    center = statistics.median(values)
    spread = mad(values)
    return {
        "n": len(values),
        "median": center,
        "mean": statistics.fmean(values),
        "stdev": statistics.stdev(values) if len(values) > 1 else 0.0,
        "min": min(values),
        "max": max(values),
        "p95": percentile(values, 0.95),
        "mad": spread,
        "relativeSpread": spread / center if center else math.inf,
    }


def bootstrap_ratio_ci(candidate: list[float], baseline: list[float],
                       seed: int = 153) -> tuple[float, float]:
    """95 % bootstrap interval of median(candidate) / median(baseline)."""
    rng = random.Random(seed)
    ratios = []
    for _ in range(BOOTSTRAP_ROUNDS):
        c = statistics.median(rng.choices(candidate, k=len(candidate)))
        b = statistics.median(rng.choices(baseline, k=len(baseline)))
        if b:
            ratios.append(c / b)
    ratios.sort()
    return ratios[int(0.025 * len(ratios))], ratios[int(0.975 * len(ratios)) - 1]


def usable(values: list[float]) -> Optional[str]:
    if len(values) < MIN_SAMPLES:
        return f"only {len(values)} samples (minimum {MIN_SAMPLES})"
    summary = summarize(values)
    if summary["relativeSpread"] > MAX_RELATIVE_SPREAD:
        return (f"relative spread {summary['relativeSpread']:.1%} exceeds "
                f"{MAX_RELATIVE_SPREAD:.0%}")
    return None


def condition_mismatches(candidate: dict, baseline: dict) -> list[str]:
    a, b = candidate.get("conditions", {}), baseline.get("conditions", {})
    return [name for name in MATCHED_CONDITIONS if a.get(name) != b.get(name)]


def evaluate_budget(budget_id: str, candidate: dict, baseline: Optional[dict]) -> dict:
    budget = BUDGETS[budget_id]
    metric = budget["metric"]
    result = {"budget": budget_id, "metric": metric, "text": budget["text"]}
    values = candidate.get("metrics", {}).get(metric)
    if not values:
        return {**result, "verdict": "NOT_MEASURED"}
    if candidate.get("conditions", {}).get("validationRun"):
        return {**result, "verdict": "INSUFFICIENT",
                "reason": "validation run on a busy or attended host"}
    if metric in ("startup_warm_ms", "startup_first_launch_ms") and \
            candidate.get("conditions", {}).get("scenarioVersion") != 2:
        return {**result, "verdict": "INSUFFICIENT",
                "reason": "startup samples predate first-paint scenario version 2"}
    guard = candidate.get("runtimeGuard", {})
    if not isinstance(guard, dict) or guard.get("completed") is not True or guard.get("cancelled") is not False:
        return {**result, "verdict": "INSUFFICIENT",
                "reason": "missing completed runtime lease monitoring"}
    proof_problem = build_evidence.budget_problem(candidate)
    if proof_problem:
        return {**result, "verdict": "INSUFFICIENT", "reason": proof_problem}
    problem = usable(values)
    if budget["kind"] == "absolute":
        if problem:
            return {**result, "verdict": "INSUFFICIENT", "reason": problem}
        observed = summarize(values)[budget.get("statistic", "median")]
        return {**result, "observed": observed, "limit": budget["limit"],
                "verdict": "PASS" if observed < budget["limit"] else "FAIL"}

    base_values = (baseline or {}).get("metrics", {}).get(metric)
    if not base_values:
        return {**result, "verdict": "INSUFFICIENT", "reason": "no baseline samples"}
    if metric in ("startup_warm_ms", "startup_first_launch_ms") and \
            baseline.get("conditions", {}).get("scenarioVersion") != 2:
        return {**result, "verdict": "INSUFFICIENT",
                "reason": "baseline startup samples predate first-paint scenario version 2"}
    guard = baseline.get("runtimeGuard", {})
    if not isinstance(guard, dict) or guard.get("completed") is not True or guard.get("cancelled") is not False:
        return {**result, "verdict": "INSUFFICIENT",
                "reason": "baseline lacks completed runtime lease monitoring"}
    proof_problem = build_evidence.budget_problem(baseline, baseline=True)
    if proof_problem:
        return {**result, "verdict": "INSUFFICIENT", "reason": proof_problem}
    if candidate["buildEvidence"]["comparison"] != baseline["buildEvidence"]["comparison"]:
        return {**result, "verdict": "INSUFFICIENT",
                "reason": "build configuration, Chromium pin, toolchain or PGO differs"}
    problem = problem or usable(base_values)
    mismatched = condition_mismatches(candidate, baseline)
    if mismatched:
        problem = "conditions differ: " + ", ".join(mismatched)
    if problem:
        return {**result, "verdict": "INSUFFICIENT", "reason": problem}

    cand, base = summarize(values), summarize(base_values)
    if budget["kind"] == "idle":
        # "No measurable own idle CPU": the candidate median may not exceed the
        # baseline median by more than three baseline MADs and the absolute floor.
        excess = cand["median"] - base["median"]
        allowed = max(3 * base["mad"], budget["limit"])
        return {**result, "candidateMedian": cand["median"], "baselineMedian": base["median"],
                "excess": excess, "allowed": allowed,
                "verdict": "PASS" if excess <= allowed else "FAIL"}

    ratio = cand["median"] / base["median"]
    low, high = bootstrap_ratio_ci(values, base_values)
    worsening = ratio - 1 if budget["direction"] == "lower" else 1 - ratio
    # Conservative: the bound of the interval on the worse side must stay in budget.
    worst = (high - 1) if budget["direction"] == "lower" else (1 - low)
    return {**result, "candidateMedian": cand["median"], "baselineMedian": base["median"],
            "worsening": worsening, "worseningBound95": worst, "limit": budget["limit"],
            "verdict": "PASS" if worst <= budget["limit"] else "FAIL"}


def evaluate(candidate: dict, baseline: Optional[dict]) -> dict:
    verdicts = [evaluate_budget(budget_id, candidate, baseline) for budget_id in BUDGETS]
    return {
        "kind": "ahoi-perf-evaluation",
        "candidate": candidate.get("app"),
        "baseline": (baseline or {}).get("app"),
        "summaries": {metric: summarize(values)
                      for metric, values in candidate.get("metrics", {}).items()},
        "verdicts": verdicts,
        # Overall H3 acceptance requires every evaluated budget. Per-budget
        # PASS remains visible for focused runs, but an omitted scenario must
        # never turn the aggregate into a complete measurement pass.
        "pass": all(v["verdict"] == "PASS" for v in verdicts),
    }
