# shellcheck shell=bash disable=SC2034
# Shared helpers of the H3 Workspace-switch trace drivers (sourced, not run).
# HID-free: only AX actions of a PREBUILT axtool (tools/desktop_e2e/axtool.swift).
# A driver never compiles it: a compiler process would cancel the leased run.
# The labels are the German product UI used by the Desktop E2E journeys.
set -u
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}
PID=${AHOI_PERF_PID:?AHOI_PERF_PID missing}
SECOND=${AHOI_PERF_SECOND_WORKSPACE:-Perf B}
[ -x "$AX" ] || { echo "prebuilt axtool missing at $AX" >&2; exit 5; }

waitax() { # <extended regex> <seconds>
  local end=$(( $(date +%s) + $2 ))
  while [ "$(date +%s)" -lt "$end" ]; do
    "$AX" dump "$PID" 14 | grep -q -E "$1" && return 0
    sleep 0.2
  done
  return 1
}

active_workspace() { # prints the active Workspace name from its switcher button
  "$AX" dump "$PID" 14 | grep -oE '[^|]+, Workspace wechseln' | head -1 \
    | sed -E 's/^ *//; s/, Workspace wechseln$//'
}

# The harness starts the browser in the background; AX menus and posted keys
# only reach a frontmost window (validation ws10/cb8, 28 Sep). AX activation
# does not reset HIDIdleTime (measured).
front() { "$AX" activate "$PID" >/dev/null 2>&1; sleep 0.3; }

open_menu() { # <active workspace name> <menu item regex>
  front
  for _ in 1 2 3; do
    "$AX" press "$PID" "$1, Workspace wechseln" AXShowMenu >/dev/null || return 1
    waitax "AXMenuItem \\| $2" 4 && return 0
    "$AX" press "$PID" "$1, Workspace wechseln" AXCancel >/dev/null 2>&1 || true
  done
  [ -n "${AHOI_PERF_TRACE_DIR:-}" ] && "$AX" dump "$PID" 14 > "$AHOI_PERF_TRACE_DIR/ax-menu-failure.txt" 2>&1
  return 1
}

menu_item() { # <workspace name>: full item title (a level suffix may follow)
  "$AX" dump "$PID" 14 | grep -oE "AXMenuItem \| $1( – [^|]*)? \|" | head -1 \
    | sed -E 's/^AXMenuItem \| //; s/ \|$//'
}
