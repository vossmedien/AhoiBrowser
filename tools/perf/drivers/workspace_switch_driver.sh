#!/bin/bash
# H3 --driver: alternate between two prepared Workspaces through the switcher
# menu (AXShowMenu + AXPress, no HID), waiting for each switch to become
# visible in the AX tree. AHOI_PERF_SWITCHES (default 20) samples per run.
. "$(dirname "$0")/workspace_common.sh"
STATE="${AHOI_PERF_STATE_DIR:-/private/tmp}/ahoi-perf-first-workspace.$PID"
FIRST=$(cat "$STATE" 2>/dev/null) || fail "run workspace_switch_setup.sh first"
rm -f "$STATE"
current=$(active_workspace); [ "$current" = "$SECOND" ] || fail "unexpected start $current"
for i in $(seq 1 "${AHOI_PERF_SWITCHES:-20}"); do
  if [ "$current" = "$FIRST" ]; then target=$SECOND; else target=$FIRST; fi
  open_menu "$current" "$target" || fail "switch menu $i did not open"
  item=$(menu_item "$target"); [ -n "$item" ] || fail "no item for $target"
  "$AX" press "$PID" "$item" >/dev/null || exit 4
  waitax "$target, Workspace wechseln" 5 || fail "switch $i to $target not visible"
  current=$target
  sleep 1  # separate samples; idle frames between switches
done
