#!/usr/bin/env python3
"""Check that commits respect the parallel agent lanes in config/agent-lanes.json.

A commit declares its lane with a `Lane: <name>` trailer. For each lane with an
`exclusive` list (currently crest-hardening), the check fails when

* a commit of that lane changes a path outside its exclusive,
  pointer-insertion or owner-writable paths, or touches a forbidden path, or
* a commit of any other lane (or without a trailer) changes one of its
  exclusive paths, except the `writableByOwners` handoff status files.

Pointer insertions into foreign files are allowed but reported, because only
added lines are permitted there.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
from dataclasses import dataclass, field

ROOT = pathlib.Path(__file__).resolve().parents[1]


def glob_to_regex(pattern: str) -> re.Pattern[str]:
    parts = []
    i = 0
    while i < len(pattern):
        if pattern.startswith("**", i):
            parts.append(".*")
            i += 2
        elif pattern[i] == "*":
            parts.append("[^/]*")
            i += 1
        else:
            parts.append(re.escape(pattern[i]))
            i += 1
    return re.compile("^" + "".join(parts) + "$")


def matches(path: str, patterns: list[str]) -> bool:
    return any(glob_to_regex(pattern).match(path) for pattern in patterns)


@dataclass
class Commit:
    sha: str
    lane: str | None
    paths: list[str]
    deletions: dict[str, int] = field(default_factory=dict)


@dataclass
class Finding:
    level: str
    sha: str
    lane: str | None
    path: str
    reason: str

    def render(self) -> str:
        lane = self.lane or "<no trailer>"
        return f"{self.level}: {self.sha[:10]} [{lane}] {self.path}: {self.reason}"


def check_commits(config: dict, commits: list[Commit]) -> list[Finding]:
    findings: list[Finding] = []
    guarded = {
        name: lane for name, lane in config["lanes"].items() if "exclusive" in lane
    }
    for commit in commits:
        for path in commit.paths:
            for name, lane in guarded.items():
                exclusive = lane.get("exclusive", [])
                owner_writable = lane.get("writableByOwners", [])
                pointers = lane.get("pointerInsertionsOnly", [])
                forbidden = lane.get("forbidden", [])
                if commit.lane == name:
                    if matches(path, forbidden):
                        findings.append(Finding("ERROR", commit.sha, commit.lane, path,
                                                f"forbidden for lane {name}"))
                    elif matches(path, pointers):
                        level = "ERROR" if commit.deletions.get(path, 0) else "NOTE"
                        reason = ("pointer file with deleted lines"
                                  if level == "ERROR" else "pointer insertion; review hunk")
                        findings.append(Finding(level, commit.sha, commit.lane, path, reason))
                    elif not matches(path, exclusive + owner_writable):
                        findings.append(Finding("ERROR", commit.sha, commit.lane, path,
                                                f"outside lane {name} allowlist"))
                elif matches(path, exclusive) and not matches(path, owner_writable):
                    findings.append(Finding("ERROR", commit.sha, commit.lane, path,
                                            f"exclusive to lane {name}"))
    return findings


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True,
                          capture_output=True, text=True).stdout


def load_commits(since: str, trailer: str) -> list[Commit]:
    commits = []
    for sha in git("rev-list", "--reverse", f"{since}..HEAD").split():
        lanes = git("log", "-1", f"--format=%(trailers:key={trailer},valueonly)",
                    sha).split()
        numstat = git("show", "--no-renames", "--numstat", "--format=", sha)
        paths, deletions = [], {}
        for line in numstat.splitlines():
            added, deleted, path = line.split("\t", 2)
            paths.append(path)
            deletions[path] = 0 if deleted == "-" else int(deleted)
        commits.append(Commit(sha, lanes[0] if lanes else None, paths, deletions))
    return commits


def worktree_paths(config: dict) -> list[str]:
    changed = git("status", "--porcelain", "--untracked-files=all").splitlines()
    exclusive = [p for lane in config["lanes"].values() for p in lane.get("exclusive", [])]
    return [line[3:] for line in changed if matches(line[3:], exclusive)]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--since", required=True, help="base commit (exclusive)")
    parser.add_argument("--all", action="store_true",
                        help="check every lane (default and only mode)")
    parser.add_argument("--worktree", action="store_true",
                        help="also list uncommitted changes in exclusive lane paths")
    parser.add_argument("--config", default=str(ROOT / "config/agent-lanes.json"))
    args = parser.parse_args(argv)

    config = json.loads(pathlib.Path(args.config).read_text())
    findings = check_commits(config, load_commits(args.since, config["commitTrailer"]))
    for finding in findings:
        print(finding.render())
    if args.worktree:
        for path in worktree_paths(config):
            print(f"INFO: uncommitted change in lane-exclusive path {path}")
    errors = sum(1 for finding in findings if finding.level == "ERROR")
    print(f"lane check: {errors} error(s), {len(findings) - errors} note(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
