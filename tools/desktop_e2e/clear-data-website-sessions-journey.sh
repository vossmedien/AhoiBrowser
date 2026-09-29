#!/bin/bash
# usage: clear-data-website-sessions-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for adoption review A2 (crest 87d89354, patch
# 0075): clearing browsing data also reaches Workspaces with their own website
# sessions. Log in to a local fixture in "Kunde" (Eigene Website-Sitzungen)
# and in the shared "Inbox", delete cookies and cache for all time through
# Settings' clear-browsing-data handler, and check that both are logged out.
# Then log in again in Kunde on two local sites, delete one site's data
# through Settings' per-site handler and check that only that site is gone;
# the same for two sibling subdomains of a registrable domain, mapped to the
# fixture by host resolver rules, where only the named host may be cleared.
# Workspace partitions are cleared asynchronously after the handler returns,
# so the checks poll with a timeout instead of sleeping a fixed time.
# No new website-session partition directory may appear. Product default
# launch, disposable user data directory; nothing here runs automatically.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9347
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-clear-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8794}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
# login.html?<account> sets a cookie and localStorage for its own host.
printf '<title>login</title><script>const a=location.search.slice(1);document.cookie="acct="+a+"; max-age=3600; path=/";localStorage.setItem("acct",a)</script>logged in' > $P-site/login.html
printf '<title>check</title>check' > $P-site/check.html
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT
# Two sites on the same fixture: an IP address and an internal host name.
SITE=http://127.0.0.1:$SITE_PORT; OTHER=http://localhost:$SITE_PORT
# Two hosts of one registrable domain (non-default port: no HTTPS upgrade).
SUB_A=http://a.example.com:$SITE_PORT; SUB_B=http://b.example.com:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
tabs() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
launch() {
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    --host-resolver-rules="MAP a.example.com 127.0.0.1, MAP b.example.com 127.0.0.1" \
    --remote-debugging-port=$PORT about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 4; $AX activate $PID >> "$OUT/steps.txt"
}
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
# Keys go through the HID event tap like a real keyboard (see
# ws-level-deletion-journey.sh); bring the app forward and retry.
key() {
  for attempt in 1 2 3 4 5; do
    $AX activate $PID >/dev/null; sleep 0.3
    $AX hidkey $PID "$@" >> "$OUT/steps.txt" && return 0
    sleep 1
  done
  echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
}
waitax() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do $AX dump $PID 14 | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
waiturl() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do tabs | grep -q "$1" && return 0; sleep 1; done; return 1; }
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}
fail_setup() { $AX dump $PID 14 > "$OUT/ax-setup-failure.txt"; finish "$1"; quit; exit 4; }
menu() { # <active workspace name> <menu item regex>
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  for attempt in 1 2 3 4; do
    $AX press $PID "$1, Workspace wechseln" AXShowMenu >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $2" 4 && return 0
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 2
  done
  return 1
}
menuitem() { # <workspace name>
  $AX dump $PID 14 | grep -oE "AXMenuItem \| $1( – [^|]*)? \|" | head -1 | sed -E 's/^AXMenuItem \| //; s/ \|$//'
}
newws() { # <active> <name> <level radio label>
  menu "$1" "Neuer Workspace…" || fail_setup "workspace menu did not open for $2"
  $AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
  waitax "AXTextField \\| Workspace-Name" 8 || fail_setup "workspace dialog for $2 did not open"
  $AX setvalue $PID "Workspace-Name" "$2" >> "$OUT/steps.txt"; sleep 1
  $AX press $PID "AXRadioButton:$3" >> "$OUT/steps.txt"; sleep 1
  $AX press $PID "Erstellen" >> "$OUT/steps.txt"
  waitax "$2, Workspace wechseln" 8 || fail_setup "workspace $2 not active"
  local end=$(( $(date +%s) + 6 ))
  while [ $(date +%s) -lt $end ] && $AX dump $PID 14 | grep -q "AXButton | Erstellen"; do sleep 1; done
}
open_url() { # <url> ; ⌘T + type + Return
  local opened=0
  for attempt in 1 2 3; do
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1; key 36
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}
switchws() { # <active> <target>
  menu "$1" "$2" || fail_setup "menu to switch to $2 did not open"
  $AX press $PID "$(menuitem "$2")" >> "$OUT/steps.txt"; waitax "$2, Workspace wechseln" 8 || fail_setup "switch to $2 failed"
}
evaluate() { # <page url substring> <expression>
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"awaitPromise":True}))' "$2")" \
    | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'
}
account_of() { # <page url substring> -> "<cookie>|<localStorage>"
  local value; value=$(evaluate "$1" 'document.cookie+"|"+(localStorage.getItem("acct")||"")')
  echo "account $1: $value" >> "$OUT/steps.txt"; echo "$value"
}
await_account() { # <page url substring> <expected> <timeout s>; 0 once seen
  local end=$(( $(date +%s) + $3 ))
  while [ $(date +%s) -lt $end ]; do
    [ "$(account_of "$1")" = "$2" ] && return 0; sleep 1
  done
  return 1
}
partition_dirs() { local d="$P/Default/Storage/ext/ahoi"; [ -d "$d" ] && ls "$d" | sort | tr '\n' ' '; }
# Settings' own WebUI handlers, the same messages the dialog and the site
# details page send; the page must be the chrome://settings target.
settings_send() { # <settings url substring> <message> <json args>
  evaluate "$1" "import('chrome://resources/js/cr.js').then(m=>m.sendWithPromise('$2',...$3)).then(()=>'done',e=>'error '+e)"
}
settings_chrome_send() { # <settings url substring> <message> <json args>
  evaluate "$1" "chrome.send('$2',$3),'sent'"
}

launch
newws Inbox Kunde "Eigene Website-Sitzungen"
open_url "$SITE/login.html?kunde"
[ "$(account_of 'login.html?kunde')" = "acct=kunde|kunde" ] && record loginInOwnWorkspace true || record loginInOwnWorkspace false
switchws Kunde Inbox
open_url "$SITE/login.html?inbox"
[ "$(account_of 'login.html?inbox')" = "acct=inbox|inbox" ] && record loginInSharedWorkspace true || record loginInSharedWorkspace false
DIRS_BEFORE=$(partition_dirs); echo "partition dirs before: $DIRS_BEFORE" >> "$OUT/steps.txt"
[ -n "$DIRS_BEFORE" ] && record ownPartitionOnDisk true || record ownPartitionOnDisk false

# A2 step 2: Settings > Delete browsing data, cookies + cache, all time (4).
open_url "chrome://settings/clearBrowserData"
R=$(settings_send settings/clearBrowserData clearBrowsingData '[["browser.clear_data.cookies","browser.clear_data.cache"],4]')
echo "clearBrowsingData: $R" >> "$OUT/steps.txt"
[ "$R" = done ] && record clearBrowsingDataRan true || record clearBrowsingDataRan false
open_url "$SITE/check.html?inbox-after"
await_account 'check.html?inbox-after' "|" 10 && record sharedLoggedOut true || record sharedLoggedOut false
switchws Inbox Kunde
# The Workspace partitions are cleared by removals queued behind this one;
# "done" above does not wait for them.
open_url "$SITE/check.html?kunde-after"
await_account 'check.html?kunde-after' "|" 30 && record ownWorkspaceLoggedOut true || record ownWorkspaceLoggedOut false

# A2 step 4: per-site "Delete data" removes only that site in Kunde.
open_url "$SITE/login.html?site-a"
open_url "$OTHER/login.html?site-b"
[ "$(account_of 'login.html?site-a')" = "acct=site-a|site-a" ] && [ "$(account_of 'login.html?site-b')" = "acct=site-b|site-b" ] \
  && record perSiteSetup true || record perSiteSetup false
open_url "chrome://settings/content/all"
R=$(settings_chrome_send settings/content/all clearUnpartitionedUsage "[\"$SITE/\"]")
echo "clearUnpartitionedUsage: $R" >> "$OUT/steps.txt"
open_url "$SITE/check.html?site-a-after"
open_url "$OTHER/check.html?site-b-after"
await_account 'check.html?site-a-after' "|" 30 && record siteDataDeleted true || record siteDataDeleted false
[ "$(account_of 'check.html?site-b-after')" = "acct=site-b|site-b" ] && record otherSiteKept true || record otherSiteKept false

# Review follow-up: a site-details page clears its host, not its eTLD+1.
open_url "$SUB_A/login.html?sub-a"
open_url "$SUB_B/login.html?sub-b"
[ "$(account_of 'login.html?sub-a')" = "acct=sub-a|sub-a" ] && [ "$(account_of 'login.html?sub-b')" = "acct=sub-b|sub-b" ] \
  && record subdomainSetup true || record subdomainSetup false
open_url "chrome://settings/content/all"
R=$(settings_chrome_send settings/content/all clearUnpartitionedUsage "[\"$SUB_A/\"]")
echo "clearUnpartitionedUsage (subdomain): $R" >> "$OUT/steps.txt"
open_url "$SUB_A/check.html?sub-a-after"
open_url "$SUB_B/check.html?sub-b-after"
await_account 'check.html?sub-a-after' "|" 30 && record subdomainDataDeleted true || record subdomainDataDeleted false
[ "$(account_of 'check.html?sub-b-after')" = "acct=sub-b|sub-b" ] && record siblingSubdomainKept true || record siblingSubdomainKept false

# A2 step 5: no website-session partition directory was created.
DIRS_AFTER=$(partition_dirs); echo "partition dirs after: $DIRS_AFTER" >> "$OUT/steps.txt"
[ "$DIRS_BEFORE" = "$DIRS_AFTER" ] && record noPartitionCreated true || record noPartitionCreated false
tabs > "$OUT/tabs.txt"
quit
finish ""
