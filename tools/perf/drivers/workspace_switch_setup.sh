#!/bin/bash
# H3 --driver-setup: create a second shared Workspace before tracing starts, so
# creating it yields no Ahoi.Workspace.Switch sample. Leaves it active.
. "$(dirname "$0")/workspace_common.sh"
waitax ', Workspace wechseln' 60 || { echo "no Workspace switcher" >&2; exit 4; }
FIRST=$(active_workspace)
[ -n "$FIRST" ] || { echo "active Workspace unknown" >&2; exit 4; }
open_menu "$FIRST" "Neuer Workspace…" || { echo "Workspace menu did not open" >&2; exit 4; }
"$AX" press "$PID" "Neuer Workspace…" >/dev/null || exit 4
waitax "AXTextField \\| Workspace-Name" 8 || { echo "create dialog missing" >&2; exit 4; }
"$AX" setvalue "$PID" "Workspace-Name" "$SECOND" >/dev/null || exit 4
"$AX" press "$PID" "Erstellen" >/dev/null || exit 4
waitax "$SECOND, Workspace wechseln" 10 || { echo "$SECOND not active" >&2; exit 4; }
echo "$FIRST" > "${AHOI_PERF_STATE_DIR:-/private/tmp}/ahoi-perf-first-workspace.$PID"
sleep 2  # let the new Workspace settle before tracing
