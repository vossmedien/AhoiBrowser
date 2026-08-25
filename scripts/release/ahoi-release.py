#!/usr/bin/env python3
"""Repository entry point for AhoiBrowser release engineering."""

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from release.cli import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main())
