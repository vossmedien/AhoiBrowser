#!/usr/bin/env python3
"""Fail unless every non-merge commit in a range carries an author DCO sign-off."""

from __future__ import annotations

import argparse
import re
import subprocess
from dataclasses import dataclass
from typing import Optional


ZERO_SHA = "0" * 40
SIGNOFF = re.compile(
    r"^Signed-off-by:\s*(?P<name>.+?)\s*<(?P<email>[^<>\s]+)>\s*$",
    re.IGNORECASE | re.MULTILINE,
)


@dataclass(frozen=True)
class Commit:
    sha: str
    author_name: str
    author_email: str
    message: str


def output(*args: str) -> str:
    return subprocess.run(
        args, check=True, capture_output=True, text=True
    ).stdout.strip()


def commit_range(base: Optional[str], head: str) -> list[str]:
    if not re.fullmatch(r"[0-9a-fA-F]{40}", head):
        raise SystemExit("DCO head must be a 40-character Git SHA")
    if base and base != ZERO_SHA:
        if not re.fullmatch(r"[0-9a-fA-F]{40}", base):
            raise SystemExit("DCO base must be a 40-character Git SHA")
        revision = f"{base}..{head}"
    else:
        revision = head
    raw = output("git", "rev-list", "--reverse", "--no-merges", revision)
    return raw.splitlines() if raw else []


def read_commit(sha: str) -> Commit:
    raw = subprocess.run(
        ("git", "show", "-s", "--format=%H%x00%an%x00%ae%x00%B", sha),
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    values = raw.rstrip("\n").split("\0", 3)
    if len(values) != 4:
        raise SystemExit(f"cannot parse commit metadata: {sha}")
    return Commit(*values)


def has_author_signoff(commit: Commit) -> bool:
    author_name = commit.author_name.strip().casefold()
    author_email = commit.author_email.strip().casefold()
    return any(
        match.group("name").strip().casefold() == author_name
        and match.group("email").strip().casefold() == author_email
        for match in SIGNOFF.finditer(commit.message)
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base")
    parser.add_argument("--head", required=True)
    args = parser.parse_args()
    failures = []
    commits = commit_range(args.base, args.head)
    for sha in commits:
        commit = read_commit(sha)
        if not has_author_signoff(commit):
            failures.append(
                f"{commit.sha}: expected Signed-off-by: "
                f"{commit.author_name} <{commit.author_email}>"
            )
    if failures:
        raise SystemExit("DCO check failed:\n" + "\n".join(failures))
    print(f"DCO check passed for {len(commits)} non-merge commit(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
