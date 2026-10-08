# Sourced by journeys. ahoi_launch_browser <log> <args...> starts
# $APP and sets PID to the browser's main process.
# With AHOI_E2E_GUI_LAUNCH=1 the browser starts through LaunchServices
# (`open -n`) in the user's GUI session while the journey itself, and its
# Accessibility driver, stay in the calling (SSH) session. Over SSH a
# directly spawned browser cannot reach the login keychain
# (errSecInteractionNotAllowed: no saved passwords, no encrypted cookies),
# while a journey run as a GUI LaunchAgent loses the Accessibility grant.
# The args must contain --user-data-dir=<dir>; it identifies the process.
ahoi_launch_browser() {
  local log=$1; shift
  # Use Chromium's native fullscreen startup when a desktop Space's Dock
  # occludes the browser. Point-owner and receiver guards remain unchanged.
  if [ "${AHOI_E2E_START_FULLSCREEN:-0}" = 1 ]; then
    set -- --start-fullscreen "$@"
  fi
  if [ "${AHOI_E2E_GUI_LAUNCH:-0}" != 1 ]; then
    "$APP/Contents/MacOS/AhoiBrowser" "$@" >> "$log" 2>&1 &
    PID=$!
    return
  fi
  local profile="" arg
  for arg in "$@"; do
    case $arg in --user-data-dir=*) profile=${arg#--user-data-dir=} ;; esac
  done
  [ -n "$profile" ] || { echo "ahoi_launch_browser: --user-data-dir missing" >&2; PID=; return 1; }
  # A FIFO keeps appending every launch to the same log, as the direct
  # launch does; secret scans read the whole log afterwards.
  local fifo; fifo=$(mktemp -u "${TMPDIR:-/private/tmp}/ahoi-launch-log.XXXXXX")
  mkfifo "$fifo" || return 1
  (cat "$fifo" >> "$log"; rm -f "$fifo") &
  open -n -a "$APP" --stdout "$fifo" --stderr "$fifo" --args "$@"
  PID=
  local i p
  for i in $(seq 1 60); do
    for p in $(pgrep -f -- "--user-data-dir=$profile"); do
      ps -o command= -p "$p" | grep -q -- '--type=' && continue
      PID=$p; return 0
    done
    sleep 0.5
  done
  echo "ahoi_launch_browser: no browser process for $profile" >&2
  return 1
}
