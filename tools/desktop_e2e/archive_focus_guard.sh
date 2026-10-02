# Shared input guard for the two owned synthetic archive journeys.
# APP, OUT, AX, PID, P, PORT and S belong to the caller; no global app lookup.
ARCHIVE_FOCUS_ACQUIRED=false
archive_yield_focus() {
  echo 'owner focus/input returned; yielding without activation or HID' >> "$OUT/steps.txt"
  echo '{"cancelled":"owner focus/input returned","pass":false}' > "$OUT/results.json"
  cp "$OUT/results.json" "$OUT/verdict.json"
  # Close only the instance whose unique synthetic profile still matches PID.
  if ps -p "$PID" -o args= | grep -F -q -- "--user-data-dir=$P"; then
    local target
    target=$(curl -fsS --max-time 2 "http://127.0.0.1:$PORT/json" |
      python3 -c 'import json,sys;print(next((t["id"] for t in json.load(sys.stdin) if t.get("type")=="page"),""))')
    [ -z "$target" ] || node "$S/cdp.mjs" "$PORT" "$target" Browser.close '{}' > "$OUT/cancel-close.json" 2>&1
  fi
  exit 8
}
archive_check_focus() {
  [ "${AHOI_E2E_YIELD_ON_FOCUS_LOSS:-0}" = 1 ] || return 0
  if [ "$ARCHIVE_FOCUS_ACQUIRED" = true ]; then
    "$AX" focused "$PID" | head -1 | grep -q " pid=$PID target=$PID$" || archive_yield_focus
  elif [ "$(idle_seconds)" -lt 2 ]; then
    archive_yield_focus
  fi
}
archive_activate_owned() {
  archive_check_focus
  if [ "${AHOI_E2E_YIELD_ON_FOCUS_LOSS:-0}" = 1 ] && [ "$ARCHIVE_FOCUS_ACQUIRED" = true ]; then
    return 0
  fi
  "$AX" activate "$PID" >> "$OUT/steps.txt" 2>&1
  if "$AX" focused "$PID" | head -1 | grep -q " pid=$PID target=$PID$"; then
    ARCHIVE_FOCUS_ACQUIRED=true
  fi
}
