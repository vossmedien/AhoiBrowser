#!/bin/bash
# H3 --driver for PERF-03 (command_bar_ms / command_bar_presented_ms): opens the
# command bar with a Command-T key event posted to the browser process only
# (`axtool key`, CGEventPostToPid; not the HID event tap). Patch 0001 opens the
# Ahoi bar only for keyboard shortcuts; menu items and AX actions keep
# Chromium's behaviour (validation run 28 Sep: "Adresse öffnen…" did not open
# it). Process-posted keys are occasionally dropped, so it retries. It clears
# the field without a sample (AX value set is not a user edit) and inserts each
# query character through AXSelectedText, one RebuildSuggestions per character.
# Needs prebuilt AHOI_AXTOOL and AHOI_AX_INSERT (ax_insert_text.swift); German UI.
set -u
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}
INSERT=${AHOI_AX_INSERT:-/private/tmp/ahoi-ax-insert}
PID=${AHOI_PERF_PID:?AHOI_PERF_PID missing}
FIELD="Mit Google suchen oder eine URL eingeben"
[ -x "$AX" ] && [ -x "$INSERT" ] || { echo "prebuilt AX helpers missing" >&2; exit 5; }
waitax() {
  local end=$(( $(date +%s) + $2 ))
  while [ "$(date +%s)" -lt "$end" ]; do
    "$AX" dump "$PID" 14 | grep -q -E "$1" && return 0
    sleep 0.2
  done
  return 1
}
for query in ${AHOI_PERF_QUERIES:-local page fixture}; do
  if ! "$AX" dump "$PID" 14 | grep -q -E "AXWindow \\| Suchen oder URL eingeben"; then
    opened=0
    for _ in 1 2 3 4 5; do
      "$AX" key "$PID" 17 cmd >/dev/null || exit 4
      waitax "AXWindow \\| Suchen oder URL eingeben" 3 && { opened=1; break; }
    done
    [ "$opened" = 1 ] || { echo "command bar did not open" >&2; exit 4; }
  fi
  "$AX" setvalue "$PID" "$FIELD" "" >/dev/null || exit 4
  sleep 1
  "$INSERT" "$PID" "$FIELD" "$query" "${AHOI_PERF_KEY_DELAY_MS:-400}" >/dev/null || exit 4
  sleep 1
done
