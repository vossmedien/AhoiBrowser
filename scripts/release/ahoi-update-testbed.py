#!/usr/bin/env python3
"""Local Sparkle appcast/tamper testbed with a throwaway key (no publishing)."""

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from release.update_testbed import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main())
