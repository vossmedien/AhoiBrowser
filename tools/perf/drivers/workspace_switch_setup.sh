#!/bin/bash
# H3 --driver-setup: create a second shared Workspace before tracing starts, so
# creating it yields no Ahoi.Workspace.Switch sample. Leaves it active.
. "$(dirname "$0")/workspace_common.sh"
waitax ', Workspace wechseln' 60 || fail "no Workspace switcher"
FIRST=$(active_workspace)
[ -n "$FIRST" ] || fail "active Workspace unknown"
open_menu "$FIRST" "Neuer Workspace…" || fail "Workspace menu did not open"
"$AX" press "$PID" "Neuer Workspace…" >/dev/null || exit 4
waitax "AXTextField \\| Workspace-Name" 8 || fail "create dialog missing"
"$AX" setvalue "$PID" "Workspace-Name" "$SECOND" >/dev/null || exit 4
"$AX" press "$PID" "Erstellen" >/dev/null || exit 4
waitax "$SECOND, Workspace wechseln" 10 || fail "$SECOND not active"
echo "$FIRST" > "${AHOI_PERF_STATE_DIR:-/private/tmp}/ahoi-perf-first-workspace.$PID"
sleep 2  # let the new Workspace settle before tracing
