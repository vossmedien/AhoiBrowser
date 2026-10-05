#!/bin/bash
# usage: run-in-gui-session.sh <label> <logdir> <command> [args...]
# Runs a command in the logged-in user's GUI launchd domain and waits for it.
# Journeys started over SSH inherit the SSH security session: Chromium's
# Safe Storage keychain access fails with errSecInteractionNotAllowed, so
# passwords and encrypted cookies cannot be stored (http-auth and PRIV-02/03
# failed that way on 5 October 2026), and signing fails the same way. A
# LaunchAgent bootstrapped into gui/<uid> runs in the user's GUI session.
# The wrapper copies PATH and AHOI_* variables, writes <logdir>/gui.log and
# <logdir>/gui-exit-code, and returns the command's exit code.
set -u
LABEL=de.vossmedien.ahoi.gui.$1; LOGDIR=$2; shift 2
mkdir -p "$LOGDIR"; rm -f "$LOGDIR/gui-exit-code"
INNER="$LOGDIR/gui-command.sh"
{
  echo '#!/bin/bash'
  printf 'export PATH=%q\n' "$PATH"
  env | grep '^AHOI_' | while IFS='=' read -r k v; do printf 'export %s=%q\n' "$k" "$v"; done
  printf 'cd %q\n' "$PWD"
  printf '%q ' "$@"; echo
  echo "echo \$? > $(printf %q "$LOGDIR/gui-exit-code")"
} > "$INNER"
chmod +x "$INNER"
PLIST="$LOGDIR/gui-agent.plist"
cat > "$PLIST" <<P
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>$LABEL</string>
<key>ProgramArguments</key><array><string>/bin/bash</string><string>$INNER</string></array>
<key>RunAtLoad</key><true/>
<key>StandardOutPath</key><string>$LOGDIR/gui.log</string>
<key>StandardErrorPath</key><string>$LOGDIR/gui.log</string>
</dict></plist>
P
DOMAIN=gui/$(id -u)
launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null
launchctl bootstrap "$DOMAIN" "$PLIST" || exit 70
until [ -s "$LOGDIR/gui-exit-code" ]; do sleep 10; done
launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null
exit "$(cat "$LOGDIR/gui-exit-code")"
