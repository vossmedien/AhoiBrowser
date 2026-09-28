#!/bin/bash
# H3 --driver: alternate between two prepared Workspaces through the switcher
# menu (AXShowMenu + AXPress, no HID), waiting for each switch to become
# visible in the AX tree. AHOI_PERF_SWITCHES (default 20) samples per run.
. "$(dirname "$0")/workspace_common.sh"
STATE="${AHOI_PERF_STATE_DIR:-/private/tmp}/ahoi-perf-first-workspace.$PID"
FIRST=$(cat "$STATE" 2>/dev/null) || { echo "run workspace_switch_setup.sh first" >&2; exit 4; }
rm -f "$STATE"
current=$(active_workspace); [ "$current" = "$SECOND" ] || { echo "unexpected start $current" >&2; exit 4; }
for i in $(seq 1 "${AHOI_PERF_SWITCHES:-20}"); do
  if [ "$current" = "$FIRST" ]; then target=$SECOND; else target=$FIRST; fi
  open_menu "$current" "$target" || { echo "switch menu $i did not open" >&2; exit 4; }
  item=$(menu_item "$target"); [ -n "$item" ] || { echo "no item for $target" >&2; exit 4; }
  "$AX" press "$PID" "$item" >/dev/null || exit 4
  waitax "$target, Workspace wechseln" 5 || { echo "switch $i to $target not visible" >&2; exit 4; }
  current=$target
  sleep 1  # separate samples; idle frames between switches
done
