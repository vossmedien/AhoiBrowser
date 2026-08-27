#!/usr/bin/env python3
"""Canonical entry point for an atomic installed AhoiDev E2E candidate."""

from __future__ import annotations

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from development_installation import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main())
