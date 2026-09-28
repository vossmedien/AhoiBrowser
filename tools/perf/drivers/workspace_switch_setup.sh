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
# An AX value set is a programmatic SetText, which the dialog may not take as
# a user edit (ws14/ws15: dialog stayed open with the name filled). Insert the
# name as a user edit when the helper is available, then retry "Erstellen".
INSERT=${AHOI_AX_INSERT:-/private/tmp/ahoi-ax-insert}
if [ -x "$INSERT" ]; then
  "$AX" setvalue "$PID" "Workspace-Name" "" >/dev/null || exit 4
  "$INSERT" "$PID" "Workspace-Name" "$SECOND" 50 >/dev/null || fail "name insert failed"
else
  "$AX" setvalue "$PID" "Workspace-Name" "$SECOND" >/dev/null || exit 4
fi
created=0
for _ in 1 2 3; do
  "$AX" press "$PID" "Erstellen" >/dev/null || true
  waitax "$SECOND, Workspace wechseln" 5 && { created=1; break; }
done
[ "$created" = 1 ] || fail "$SECOND not active"
echo "$FIRST" > "${AHOI_PERF_STATE_DIR:-/private/tmp}/ahoi-perf-first-workspace.$PID"
sleep 2  # let the new Workspace settle before tracing
