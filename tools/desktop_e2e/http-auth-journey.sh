#!/bin/bash
# usage: http-auth-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for the HTTP-Auth gate (save, second account,
# choice/autocomplete, realm/port separation, password update, sign-out without
# restart, forget realm, no secret in logs). Synthetic loopback accounts only;
# see basic_auth_fixture.py. Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9366; A=8793; B2=8794
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p $OUT; P=$(mktemp -d /private/tmp/ahoi-auth-profile.XXXXXX); : > $OUT/steps.txt; : > $OUT/results.txt
python3 "$S/basic_auth_fixture.py" --port $A --second-port $B2 > $OUT/fixture.log 2>&1 &
FIX=$!; trap 'kill $FIX 2>/dev/null' EXIT; sleep 1
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
  --remote-debugging-port=$PORT about:blank > $OUT/browser.log 2>&1 &
PID=$!; echo "pid=$PID profile=$P" > $OUT/run.txt
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 4

ax() { $AX "$@" >> $OUT/steps.txt 2>&1; }
title() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys
p=[t for t in json.load(sys.stdin) if t["type"]=="page"]; print(p[0]["title"] if p else "")'; }
waitax() { local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do $AX dump $PID 40 | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
waittitle() { local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do title | grep -q "$1" && return 0; sleep 1; done; return 1; }
record() { echo "$1 $2" >> $OUT/results.txt; echo "== $1 $2" >> $OUT/steps.txt; }
store() { cp "$P/Default/Login Data" $OUT/.ld.db 2>/dev/null || { echo ""; return; }
  sqlite3 $OUT/.ld.db "select signon_realm||'|'||username_value from logins where scheme=1 order by 1;" | tr '\n' ' '
  rm -f $OUT/.ld.db; }
cmdbar() { local ok=1
  for i in 1 2 3; do ax activate $PID; sleep 1; ax key $PID 17 cmd
    waitax "AXWindow \| Suchen oder URL eingeben" 6 && { ok=0; break; }; done; return $ok; }
goto() { cmdbar || return 1; ax key $PID 0 cmd; ax type $PID "$1"; sleep 1; ax key $PID 36; }
# HTTP-auth commands appear in this order below the "HTTP" query; the first is
# preselected. A full-text query would instead preselect the web search row.
command() { local idx
  case "$1" in switch) idx=0;; forget) idx=1;; manage) idx=2;; esac
  cmdbar || return 1; ax type $PID "HTTP"
  waitax "AXStaticText \| HTTP-Authentifizierungskonto wechseln" 8 || return 1
  for i in $(seq 1 $idx); do ax key $PID 125; sleep 0.3; done
  ax key $PID 36; }
dialog() { waitax "AXHeading \| Anmelden" "${1:-10}"; }
login() { # <user> <password> <save-option-label or ''>
  ax focus $PID "AXTextField:Nutzername"; ax key $PID 0 cmd; ax type $PID "$1"
  ax focus $PID "AXTextField:Passwort"; ax key $PID 0 cmd; ax type $PID "$2"; sleep 1
  [ -n "$3" ] && ax press $PID "$3"
  ax press $PID "AXButton:Anmelden"; }
accounts() { dialog 2 || { echo "no-dialog"; return; }; ax focus $PID "AXTextField:Nutzername"; ax key $PID 0 cmd; ax key $PID 51; sleep 1
  ax press $PID "AXButton:Nutzername"; sleep 2
  $AX dump $PID 45 | awk '/AXMenuBar$/{exit} {print}' | grep -o -E 'AXMenuItem \| [a-z]+ \|' \
    | sort -u | awk '{print $3}' | tr '\n' ' '
  ax key $PID 53; sleep 1; }
SAVE="Zugang nach erfolgreicher Anmeldung speichern"
UPDATE="Gespeicherten Zugang nach erfolgreicher Anmeldung aktualisieren"

# 1 Save alice in Realm A.
goto "http://127.0.0.1:$A/a/"; dialog 15 && login alice alice-pass-1 "$SAVE"
waittitle "auth:alice@Ahoi Realm A:$A" 15 && record save_first PASS || record save_first FAIL
# 2 Second account via account switch.
command switch; dialog && login bob bob-pass-1 "$SAVE"
waittitle "auth:bob@Ahoi Realm A:$A" 15 && record save_second PASS || record save_second FAIL
[ "$(store)" = "http://127.0.0.1:$A/Ahoi Realm A|alice http://127.0.0.1:$A/Ahoi Realm A|bob " ] \
  && record store_two_accounts PASS || record store_two_accounts "FAIL:$(store)"
# 3 Choice + autocomplete: both listed, pick alice, password filled from store.
command switch; dialog
LIST=$(accounts); [ "$LIST" = "alice bob " ] && record choice_lists_both PASS || record choice_lists_both "FAIL:$LIST"
ax press $PID "AXButton:Nutzername"; sleep 1; ax press $PID "AXMenuItem:alice"; sleep 1
$AX dump $PID 40 | grep -q -E 'AXTextField \| Passwort \| •+' && record autocomplete_password PASS || record autocomplete_password FAIL
ax press $PID "AXButton:Anmelden"
waittitle "auth:alice@Ahoi Realm A:$A" 15 && record choose_account PASS || record choose_account FAIL
# 4 Realm separation: Realm B on the same origin offers neither account.
goto "http://127.0.0.1:$A/b/"; dialog 15
$AX dump $PID 40 | grep -q 'Realm: Ahoi Realm B' || record realm_b_prompt FAIL
LIST=$(accounts); [ -z "$LIST" ] && record realm_separation PASS || record realm_separation "FAIL:$LIST"
ax press $PID "AXButton:Abbrechen"; sleep 2
# 5 Port separation: same realm name on another port offers no account.
goto "http://127.0.0.1:$B2/a/"; dialog 15
LIST=$(accounts); [ -z "$LIST" ] && record port_separation PASS || record port_separation "FAIL:$LIST"
ax press $PID "AXButton:Abbrechen"; sleep 2
# 6 Password update: server rotates alice; old saved password is rejected
#   without deleting the account; the new one updates the same row.
curl -s http://127.0.0.1:$A/__rotate >/dev/null
goto "http://127.0.0.1:$A/a/"; sleep 3
command switch; dialog
ax press $PID "AXButton:Nutzername"; sleep 1; ax press $PID "AXMenuItem:alice"; sleep 1; ax press $PID "AXButton:Anmelden"
dialog 15 && record rejected_reprompt PASS || record rejected_reprompt FAIL
echo "$(store)" | grep -q "|alice" && record rejected_keeps_account PASS || record rejected_keeps_account FAIL
login alice alice-pass-2 "$UPDATE"
waittitle "auth:alice@Ahoi Realm A:$A" 15 && record password_update_signin PASS || record password_update_signin FAIL
[ "$(store)" = "http://127.0.0.1:$A/Ahoi Realm A|alice http://127.0.0.1:$A/Ahoi Realm A|bob " ] \
  && record update_no_duplicate PASS || record update_no_duplicate "FAIL:$(store)"
# 7 Sign out without restart: switch, then cancel the challenge -> 401 page.
command switch; dialog && ax press $PID "AXButton:Abbrechen"
waittitle "Ahoi auth required" 10 && record sign_out_without_restart PASS || record sign_out_without_restart "FAIL:$(title)"
kill -0 $PID 2>/dev/null && record same_browser_process PASS || record same_browser_process FAIL
# 8 Forget this realm: saved accounts for Realm A are removed.
goto "http://127.0.0.1:$A/a/"; dialog 15 && login bob bob-pass-1 ""
waittitle "auth:bob@Ahoi Realm A:$A" 15
command forget; sleep 4
[ -z "$(store)" ] && record forget_realm PASS || record forget_realm "FAIL:$(store)"
# 9 No password or Basic token in logs.
if grep -a -q -E 'alice-pass|bob-pass|YWxpY2U6|Ym9iOm' $OUT/browser.log $OUT/fixture.log; then
  record no_secret_in_logs FAIL; else record no_secret_in_logs PASS; fi

ax key $PID 12 cmd; sleep 5
python3 - $OUT/results.txt > $OUT/results.json <<'PY'
import json,sys
rows=[l.split(" ",1) for l in open(sys.argv[1]).read().splitlines() if l]
res={k:v for k,v in rows}
print(json.dumps({"pass":all(v=="PASS" for v in res.values()) and len(res)>=16,"steps":res},indent=1))
PY
cat $OUT/results.json
