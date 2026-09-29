#!/bin/bash
# usage: http-auth-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for the HTTP-Auth gate (DoD 9, AUTH-* in
# config/test-registry.json) on the installed candidate: save only after
# success, restart offer, two accounts, keyboard autocomplete, preferred
# account, wrong password + other account, update, realm/port/path/scheme
# (Digest) separation, proxy versus server credentials, cross-origin redirect,
# switch/sign-out (also in a Workspace with its own website sessions), single
# delete and forget realm, never-save + reset, HTTP warning without auto-login,
# incognito (explicit use only, nothing persisted, cache gone with the last
# window), subresource prompts, the credential manager's system-auth gate
# (the prompt is only observed and cancelled, never passed), and no secret in
# logs, NetLog, crash reports, evidence or profile files.
# Not drivable here: AUTH-12 (needs a trusted HTTPS certificate), AUTH-23
# (Mac B / CloudKit / iOS, assisted). Synthetic loopback accounts only; the
# Basic/Digest/proxy fixture is inline below. Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9366; A=8793; B2=8794; PX=8797
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
# The journey activates windows and posts input: never run it while the owner
# is using this Mac. Require AHOI_E2E_MIN_IDLE seconds (default 300) of no HID
# input before starting.
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
for fp in $A $B2 $PX; do
  if lsof -nP -iTCP:$fp -sTCP:LISTEN >/dev/null 2>&1; then echo "fixture port $fp busy" >&2; exit 6; fi
done
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-auth-profile.XXXXXX); : > "$OUT/steps.txt"; : > "$OUT/results.txt"
touch "$P-start"; STORE_P=$P; PID=""

# Loopback fixture: Basic realms A/B/Subresource, Digest realm A under /a/dg/
# (same realm name, other scheme), an observer for redirect/path checks and a
# Basic proxy that forwards only to the two fixture origins. Its log names
# route, status and whether an Authorization header was present, never a
# header value or a query.
FIXTURE=$(cat <<'PY'
import base64, hashlib, html, http.client, json, re, secrets, sys, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

A, B2, PX = (int(v) for v in sys.argv[1:4])
BASIC = {"Ahoi Realm A": {"alice": "alice-pass-1", "bob": "bob-pass-1"},
         "Ahoi Realm B": {"carol": "carol-pass-1"},
         "Ahoi Subresource": {"sam": "sam-pass-1"}}
ROTATED = {"alice": "alice-pass-2"}  # accepted after /__rotate
DIGEST_REALM, DIGEST = "Ahoi Realm A", {"dave": "dave-digest-1"}
PROXY_REALM, PROXY = "Ahoi Proxy", {"pia": "pia-proxy-1"}
ORIGIN_USERS = set(DIGEST) | {u for r in BASIC.values() for u in r}
NONCE, OPAQUE = secrets.token_hex(16), secrets.token_hex(8)
LOCK = threading.Lock()
STATE = {"rotated": False, "seen": {},
         "proxy": {"requests": 0, "forwarded": 0,
                   "proxy_header_with_origin_creds": 0,
                   "origin_saw_proxy_header": 0,
                   "origin_saw_proxy_creds": 0}}
HOP = {"connection", "keep-alive", "proxy-authorization", "proxy-authenticate",
       "proxy-connection", "te", "trailer", "transfer-encoding", "upgrade"}


def log(role, method, path, status, auth):
    print(json.dumps({"role": role, "method": method, "route": urlsplit(path).path,
                      "status": status, "authorization": bool(auth)}), flush=True)


def bump(key):
    with LOCK:
        STATE["proxy"][key] += 1


def basic_pair(header):
    if not header.startswith("Basic "):
        return None, None
    try:
        user, _, password = base64.b64decode(header[6:]).decode().partition(":")
    except Exception:
        return None, None
    return user, password


def basic_user(header, accounts, rotated=False):
    user, password = basic_pair(header)
    expected = accounts.get(user)
    if rotated and user in ROTATED:
        expected = ROTATED[user]
    return user if expected is not None and password == expected else None


def digest_user(header, method, path):
    if not header.startswith("Digest "):
        return None
    p = {m.group(1).lower(): (m.group(2) if m.group(2) is not None else m.group(3))
         for m in re.finditer(r'(\w+)=(?:"([^"]*)"|([^\s,]*))', header[7:])}
    user = p.get("username")
    password = DIGEST.get(user)
    if password is None or p.get("realm") != DIGEST_REALM or p.get("nonce") != NONCE \
            or p.get("uri") != path:
        return None
    h = lambda s: hashlib.md5(s.encode()).hexdigest()
    ha1, ha2 = h(f"{user}:{DIGEST_REALM}:{password}"), h(f"{method}:{path}")
    if p.get("qop") == "auth":
        expected = h(f"{ha1}:{NONCE}:{p.get('nc', '')}:{p.get('cnonce', '')}:auth:{ha2}")
    else:
        expected = h(f"{ha1}:{NONCE}:{ha2}")
    return user if secrets.compare_digest(expected, p.get("response", "")) else None


class Origin(BaseHTTPRequestHandler):
    server_version = "AhoiAuthFixture/2"

    def log_message(self, fmt, *args):  # never log Authorization headers
        return

    def raw(self, code, body, ctype="text/html; charset=utf-8", extra=None):
        payload = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        for key, value in (extra or {}).items():
            self.send_header(key, value)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
        return code

    def page(self, code, title, body, extra=None):
        return self.raw(code, f"<!doctype html><title>{html.escape(title)}</title>"
                              f"<h1>{html.escape(body)}</h1>", extra=extra)

    def do_GET(self):
        auth = self.headers.get("Authorization", "")
        if self.headers.get("Proxy-Authorization"):
            bump("origin_saw_proxy_header")
        if basic_pair(auth)[0] in PROXY:
            bump("origin_saw_proxy_creds")
        log("origin", "GET", self.path, self.route(auth), auth)

    def route(self, auth):
        port = self.server.server_address[1]
        url = urlsplit(self.path)
        path, query = url.path, parse_qs(url.query)
        key = query.get("k", [None])[0]
        if key:
            with LOCK:
                STATE["seen"][key] = bool(auth)
        if path == "/__health":
            return self.page(200, "ok", "ok")
        if path == "/__rotate":
            STATE["rotated"] = True
            return self.page(200, "rotated", "alice password rotated")
        if path == "/__seen":
            with LOCK:
                v = STATE["seen"].get(query.get("key", [""])[0])
            return self.raw(200, "none" if v is None else ("1" if v else "0"), "text/plain")
        if path == "/__proxy-report":
            with LOCK:
                return self.raw(200, json.dumps(STATE["proxy"]), "application/json")
        if path == "/pub/img.html":
            return self.raw(200, "<!doctype html><title>subres-same</title>"
                                 "<img src=\"/s/pixel.svg\" alt=\"protected\">")
        # Same site, other port: Chromium only blocks cross-site subresource
        # prompts (blink IsBannedCrossSiteAuth, site_for_cookies ignores the
        # port). localhost versus 127.0.0.1 is cross-site.
        if path == "/pub/img-cross.html":
            return self.raw(200, "<!doctype html><title>subres-cross</title>"
                                 f"<img src=\"http://127.0.0.1:{B2}/s/pixel.svg?k=subx-port\" alt=\"protected\">")
        if path == "/pub/img-site.html":
            return self.raw(200, "<!doctype html><title>subres-site</title>"
                                 f"<img src=\"http://localhost:{B2}/s/pixel.svg?k=subx-site\" alt=\"protected\">")
        if path.startswith("/pub/"):
            return self.page(200, f"public:{key}", "public page")
        if path.startswith("/s/"):
            if basic_user(auth, BASIC["Ahoi Subresource"]) is None:
                return self.page(401, "Ahoi auth required", "subresource login required",
                                 {"WWW-Authenticate": 'Basic realm="Ahoi Subresource", charset="UTF-8"'})
            return self.raw(200, '<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"/>',
                            "image/svg+xml")
        if path.startswith("/a/dg/"):
            user = digest_user(auth, "GET", self.path)
            if user is None:
                challenge = (f'Digest realm="{DIGEST_REALM}", qop="auth", algorithm=MD5, '
                             f'nonce="{NONCE}", opaque="{OPAQUE}"')
                return self.page(401, "Ahoi auth required", "digest login required",
                                 {"WWW-Authenticate": challenge})
            return self.page(200, f"digest:{user}@{DIGEST_REALM}:{port}", f"signed in as {user}")
        if path.startswith("/a/") or path.startswith("/z/"):
            realm = "Ahoi Realm A"
        elif path.startswith("/b/"):
            realm = "Ahoi Realm B"
        else:
            return self.page(200, "Ahoi auth fixture", "public page")
        user = basic_user(auth, BASIC[realm], STATE["rotated"] and realm == "Ahoi Realm A")
        if user is None:
            return self.page(401, "Ahoi auth required", f"login required for {realm}",
                             {"WWW-Authenticate": f'Basic realm="{realm}", charset="UTF-8"'})
        if path == "/a/redirect-cross":
            return self.raw(302, "", extra={
                "Location": f"http://127.0.0.1:{B2}/pub/observe?k=redirect-cross"})
        return self.page(200, f"auth:{user}@{realm}:{port}", f"signed in as {user}")


class Proxy(BaseHTTPRequestHandler):
    server_version = "AhoiAuthProxy/1"

    def log_message(self, fmt, *args):
        return

    def reply(self, code, extra=None):
        self.send_response(code)
        for key, value in (extra or {}).items():
            self.send_header(key, value)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()
        return code

    def do_CONNECT(self):
        log("proxy", "CONNECT", "/", self.reply(403), False)

    def do_GET(self):
        bump("requests")
        header = self.headers.get("Proxy-Authorization", "")
        if basic_pair(header)[0] in ORIGIN_USERS:
            bump("proxy_header_with_origin_creds")
        target = urlsplit(self.path)
        if basic_user(header, PROXY) is None:
            status = self.reply(407, {"Proxy-Authenticate": f'Basic realm="{PROXY_REALM}"'})
        elif target.hostname != "127.0.0.1" or target.port not in (A, B2):
            status = self.reply(403)
        else:
            bump("forwarded")
            headers = {k: v for k, v in self.headers.items() if k.lower() not in HOP}
            conn = http.client.HTTPConnection("127.0.0.1", target.port, timeout=10)
            conn.request("GET", target.path + ("?" + target.query if target.query else ""),
                         headers=headers)
            response = conn.getresponse()
            body = response.read()
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in HOP and key.lower() != "content-length":
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
            status = response.status
        log("proxy", "GET", target.path or "/", status, header)


servers = [ThreadingHTTPServer(("127.0.0.1", A), Origin),
           ThreadingHTTPServer(("127.0.0.1", B2), Origin),
           ThreadingHTTPServer(("127.0.0.1", PX), Proxy)]
for server in servers[1:]:
    threading.Thread(target=server.serve_forever, daemon=True).start()
print(f"ready origins {A} {B2} proxy {PX}", flush=True)
servers[0].serve_forever()
PY
)
python3 -u -c "$FIXTURE" $A $B2 $PX > "$OUT/fixture.log" 2>&1 &
FIX=$!
# On exit stop the fixture and, only while that PID is still AhoiBrowser
# (never a reused PID), the browser.
trap 'kill $FIX 2>/dev/null; [ -n "$PID" ] && ps -p $PID -o comm= 2>/dev/null | grep -q AhoiBrowser && kill $PID; rm -f "$P-canaries" "$P-ld.db"' EXIT
for i in $(seq 1 20); do curl -s http://127.0.0.1:$A/__health >/dev/null && break; sleep 0.5; done

# Each launch logs to browser.log and writes its own NetLog (default capture
# mode) so AUTH-25 can scan both after the run.
launch() { # <label> [extra flags...]
  local label=$1; shift
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$STORE_P --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT --enable-logging=stderr \
    --vmodule=login_handler=1,login_tab_helper=1,http_auth*=1 \
    --log-net-log="$OUT/netlog-$label.json" "$@" about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$STORE_P launch=$label" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 4
}
launch main

# Keys go through the HID event tap like a real keyboard (keys posted to the
# process are intermittently dropped by Chromium); hidkey refuses unless the
# app is frontmost, so bring it forward and retry.
ax() {
  if [ "$1" = key ]; then
    shift; local pid=$1; shift
    for attempt in 1 2 3 4 5; do
      "$AX" activate "$pid" >/dev/null 2>&1; sleep 0.3
      "$AX" hidkey "$pid" "$@" >> "$OUT/steps.txt" 2>&1 && return 0
      sleep 1
    done
    echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
  fi
  "$AX" "$@" >> "$OUT/steps.txt" 2>&1
}
title() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys
p=[t for t in json.load(sys.stdin) if t["type"]=="page"]; print(p[0]["title"] if p else "")'; }
waitax() { local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do $AX dump $PID 40 | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
waittitle() { local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do title | grep -q "$1" && return 0; sleep 1; done; return 1; }
record() { echo "$1 $2" >> "$OUT/results.txt"; echo "== $1 $2" >> "$OUT/steps.txt"; }
# rec <step> <command...>: PASS when the command succeeds.
rec() { local n=$1; shift; if "$@"; then record "$n" PASS; else record "$n" FAIL; fi; }
alive() { kill -0 $PID 2>/dev/null; }
has_ax() { $AX dump $PID 40 | grep -q -E -- "$1"; }
no_ax() { alive && ! $AX dump $PID 40 | grep -q -E -- "$1"; }
# Login Data rows (signon realm | username) of one PasswordForm scheme
# (1 Basic, 2 Digest). The copy lives next to the profile, never in $OUT.
store_q() { cp "$STORE_P/Default/Login Data" "$P-ld.db" 2>/dev/null || { echo ""; return; }
  sqlite3 "$P-ld.db" "select signon_realm||'|'||username_value from logins where scheme=$1 order by 1;" | tr '\n' ' '
  rm -f "$P-ld.db"; }
store() { store_q 1; }
# The command bar is key while it is open, so Escape reaches only the bar.
bar_open() { $AX dump $PID 3 | grep -q 'AXWindow | Suchen oder URL eingeben'; }
closed_cmdbar() { local end=$(( $(date +%s) + 10 ))
  while [ $(date +%s) -lt $end ]; do $AX dump $PID 3 | grep -q 'Suchen oder URL eingeben' || return 0; sleep 1; done
  return 1; }
close_cmdbar() { bar_open || return 0
  ax key $PID 53; closed_cmdbar && return 0
  echo "-- command bar did not close on Escape" >> "$OUT/steps.txt"; return 1; }
# A bar that ignored Return is kept as AX evidence, then closed, so it can no
# longer swallow the next steps (build 50: one open bar failed 12 steps).
STUCK=0
stuck_cmdbar() { STUCK=$((STUCK+1))
  echo "-- command bar still open after Return ($1); see ax-cmdbar-stuck-$STUCK.txt" >> "$OUT/steps.txt"
  $AX dump $PID 40 > "$OUT/ax-cmdbar-stuck-$STUCK.txt" 2>&1; close_cmdbar; }
# Never press ⌘T into an open bar: it re-creates the bubble on the same anchor
# ("anchor_view has already anchored a focusable widget", build 50), and the
# re-created bar never executed Return there.
cmdbar() { local ok=1
  close_cmdbar
  for i in 1 2 3; do ax activate $PID; sleep 1; ax key $PID 17 cmd
    waitax "AXWindow \| Suchen oder URL eingeben" 6 && { ok=0; break; }; done
  sleep 1; return $ok; }
# Types into the open command bar and checks that its text field really holds
# the text; a first keystroke can arrive before the field has focus (proven in
# keyboard-shortcuts-journey.sh, build 47).
type_in() {
  for attempt in 1 2 3; do
    ax type $PID "$1"; sleep 1
    AHOI_AX_VALUE_MAX=300 $AX dump $PID 14 | grep "AXTextField" | grep -F -q -- "| $1" && return 0
    echo "info: typed text missing, retyping" >> "$OUT/steps.txt"
    ax key $PID 0 cmd; sleep 0.5
  done
  return 1; }
goto() { cmdbar || { echo "-- command bar did not open for $1" >> "$OUT/steps.txt"; return 1; }
  ax key $PID 0 cmd
  type_in "$1" || { echo "-- could not type $1" >> "$OUT/steps.txt"; close_cmdbar; return 1; }
  ax key $PID 36
  closed_cmdbar && return 0
  stuck_cmdbar "goto $1"; return 1; }
# A challenge the harness disturbed shows only the 401 page; one explicit reload
# re-issues it. Every use is recorded so a product-side cancel stays visible.
challenge() { dialog "${1:-30}" && return 0
  title | grep -q "Ahoi auth required" || return 1
  echo "-- reload to re-issue challenge" >> "$OUT/steps.txt"; echo reload >> "$OUT/reloads.txt"
  ax activate $PID; ax key $PID 15 cmd; dialog 20; }
# HTTP-auth commands appear below the "HTTP" query; the first is preselected.
# A full-text query would instead preselect the web search row. The row index
# is taken from the offered rows, because "forget" and "manage" are hidden
# where they cannot run (no active realm, incognito). Every failure closes the
# bar again.
command() { local want order idx
  case "$1" in
    switch) want="HTTP-Authentifizierungskonto wechseln";;
    forget) want="Gespeicherte HTTP-Zugangsdaten für diesen Schutzbereich vergessen";;
    manage) want="Gespeicherte HTTP-Zugänge verwalten";;
  esac
  cmdbar || { echo "-- command bar did not open for $1" >> "$OUT/steps.txt"; return 1; }
  type_in "HTTP" || { echo "-- could not type the $1 query" >> "$OUT/steps.txt"; close_cmdbar; return 1; }
  if ! waitax "AXStaticText \| (HTTP-Authentifizierungskonto wechseln|Gespeicherte HTTP-Zugänge verwalten)" 8; then
    echo "-- no HTTP-auth rows for $1" >> "$OUT/steps.txt"; close_cmdbar; return 1
  fi
  sleep 0.5
  order=$($AX dump $PID 40 | grep -o -E "AXStaticText \| (HTTP-Authentifizierungskonto wechseln|Gespeicherte HTTP-Zugangsdaten für diesen Schutzbereich vergessen|Gespeicherte HTTP-Zugänge verwalten)" \
    | sed 's/^AXStaticText | //' | awk '!seen[$0]++')
  idx=$(printf '%s\n' "$order" | grep -n -x -F "$want" | cut -d: -f1)
  if [ -z "$idx" ]; then
    echo "-- command $1 not offered; rows: $(echo $order)" >> "$OUT/steps.txt"; close_cmdbar; return 1
  fi
  # Not `seq 1 $idx`: BSD seq counts down, so idx=0 pressed Down twice.
  local i=1; while [ $i -lt $idx ]; do ax key $PID 125; sleep 0.3; i=$((i+1)); done
  ax key $PID 36
  closed_cmdbar || { stuck_cmdbar "command $1"; return 1; }; }
dialog() { waitax "AXHeading \| Anmelden" "${1:-20}" && return 0
  { echo "-- dialog timeout; windows and page:"; $AX dump $PID 3 | grep AXWindow; title; } >> "$OUT/steps.txt"
  return 1; }
login() { # <user> <password> <save-option-label or ''>
  ax focus $PID "AXTextField:Nutzername" || { echo "-- login $1: no dialog" >> "$OUT/steps.txt"; return 1; }
  ax key $PID 0 cmd; ax type $PID "$1"
  ax focus $PID "AXTextField:Passwort"; ax key $PID 0 cmd; ax type $PID "$2"; sleep 1
  [ -n "$3" ] && ax press $PID "$3"
  ax press $PID "AXButton:Anmelden"; }
menu_items() { $AX dump $PID 45 | awk '/AXMenuBar$/{exit} {print}' | grep -o -E 'AXMenuItem \| [a-z]+ \|' \
    | sort -u | awk '{print $3}' | tr '\n' ' '; }
# The username field is a Views EditableCombobox (filter_on_edit, show_on_empty):
# its menu lists only accounts starting with the field's text, so a prefilled
# "alice" hides "bob". Clearing the field opens the full list by itself; the
# arrow button toggles the menu. Never close it with Escape: without an open
# menu Escape cancels the login dialog (build 50 lost the realm-B, port,
# Digest, /z/, preferred-account and proxy dialogs that way).
menu_open() { [ -n "$(menu_items)" ]; }
open_account_menu() {
  ax focus $PID "AXTextField:Nutzername" || return 1
  ax key $PID 0 cmd; ax key $PID 51
  local end=$(( $(date +%s) + 2 ))
  while [ $(date +%s) -lt $end ]; do menu_open && return 0; sleep 0.5; done
  ax press $PID "AXButton:Nutzername"
  end=$(( $(date +%s) + 3 ))
  while [ $(date +%s) -lt $end ]; do menu_open && return 0; sleep 0.5; done
  return 1; }
close_account_menu() { menu_open || return 0
  ax press $PID "AXButton:Nutzername"; sleep 1
  menu_open || return 0
  # Only while the menu is open: then the menu controller consumes Escape.
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  menu_open && { echo "-- account menu still open" >> "$OUT/steps.txt"; return 1; }; return 0; }
accounts() { dialog 2 || { echo "no-dialog"; return; }
  open_account_menu; sleep 1
  menu_items
  close_account_menu
  dialog 2 || echo "-- login dialog lost while listing accounts" >> "$OUT/steps.txt"; }
pick() { # <username>: choose a saved account from the unfiltered list
  open_account_menu || { echo "-- account menu did not open for $1" >> "$OUT/steps.txt"; return 1; }
  ax press $PID "AXMenuItem:$1" || { close_account_menu; return 1; }
  sleep 1; }
# Between sections: close a stale command bar and cancel a login dialog an
# earlier failure left open, so one failure cannot fail the next section.
settle() { close_cmdbar
  local i; for i in 1 2 3; do
    has_ax "AXHeading \| Anmelden" || return 0
    echo "-- settle: cancelling a leftover login dialog" >> "$OUT/steps.txt"
    ax press $PID "AXButton:Abbrechen"; sleep 2
  done; }
# Username the dialog shows now (the value is the last AX field; none -> "").
prefilled() { $AX dump $PID 40 | grep -m1 'AXTextField | Nutzername' \
    | awk -F' [|] ' '{v=$NF; gsub(/ +$/,"",v); if (v=="Nutzername") v=""; print v}'; }
# Title of the first page whose URL contains the marker.
realm_title() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys
p=[t for t in json.load(sys.stdin) if t["type"]=="page" and sys.argv[1] in t["url"]]
print(p[0]["title"] if p else "<no tab>")' "$1"; }
wait_rt() { # <url marker> <expected title> <seconds>
  local end=$(( $(date +%s) + $3 ))
  while [ $(date +%s) -lt $end ]; do [ "$(realm_title "$1")" = "$2" ] && return 0; sleep 1; done
  echo "-- $1 title: $(realm_title "$1")" >> "$OUT/steps.txt"; return 1; }
seen() { curl -s "http://127.0.0.1:$A/__seen?key=$1"; }
wait_seen() { local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do [ "$(seen "$1")" != none ] && return 0; sleep 1; done; return 1; }
eval_in() { # <url substring> <expression>
  node "$S/cdp.mjs" $PORT "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True}))' "$2")" >> "$OUT/steps.txt" 2>&1; }
quit() { ax key $PID 12 cmd; for i in $(seq 1 20); do alive || return 0; sleep 1; done
  echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
SAVE="Zugang nach erfolgreicher Anmeldung speichern"
UPDATE="Gespeicherten Zugang nach erfolgreicher Anmeldung aktualisieren"
NEVER="Zugänge für diesen Schutzbereich nie speichern"
USE_SAVED="Gespeichertes Konto verwenden"
FAILED_TEXT="Anmeldung fehlgeschlagen"
HTTP_WARNING="Warnung: HTTP schützt diese Zugangsdaten"

# ---- Phase 1: one browser session ------------------------------------------
# 1 Save alice in Realm A. AUTH-01: a failed attempt with "save" selected
#   stores nothing; AUTH-19: the HTTP dialog warns about the transport.
goto "http://127.0.0.1:$A/a/"; challenge
rec auth19_http_warning has_ax "$HTTP_WARNING"
login alice alice-wrong-0 "$SAVE"; dialog 30; sleep 3
rec auth01_not_saved_before_success eval 'alive && has_ax "AXHeading \| Anmelden" && [ -z "$(store)" ]'
login alice alice-pass-1 "$SAVE"
waittitle "auth:alice@Ahoi Realm A:$A" 15 && record save_first PASS || record save_first FAIL
# 2 Second account via account switch (AUTH-15: the switch reloads into the
#   account chooser without a harness reload).
command switch
if dialog; then record auth15_switch_shows_chooser PASS; login bob bob-pass-1 "$SAVE"
else record auth15_switch_shows_chooser FAIL; fi
waittitle "auth:bob@Ahoi Realm A:$A" 15 && record save_second PASS || record save_second FAIL
[ "$(store)" = "http://127.0.0.1:$A/Ahoi Realm A|alice http://127.0.0.1:$A/Ahoi Realm A|bob " ] \
  && record store_two_accounts PASS || record store_two_accounts "FAIL:$(store)"
# 3 Choice + autocomplete: both listed, pick alice, password filled from store.
settle; command switch; dialog
LIST=$(accounts); [ "$LIST" = "alice bob " ] && record choice_lists_both PASS || record choice_lists_both "FAIL:$LIST"
pick alice
$AX dump $PID 40 | grep -q -E 'AXTextField \| Passwort \| •+' && record autocomplete_password PASS || record autocomplete_password FAIL
ax press $PID "AXButton:Anmelden"
waittitle "auth:alice@Ahoi Realm A:$A" 15 && record choose_account PASS || record choose_account FAIL
# 3b AUTH-04: type a username prefix, the list filters to bob, select it with
#    Down + Return; the saved password fills in. Without a dialog nothing here
#    can pass (build 50 recorded a PASS for a Return into the command bar).
settle; command switch
if dialog; then
  ax focus $PID "AXTextField:Nutzername"; ax key $PID 0 cmd; ax key $PID 51; sleep 0.5
  ax type $PID "b"; sleep 2
  LIST=$(menu_items); [ "$LIST" = "bob " ] && record auth04_filter_by_typing PASS || record auth04_filter_by_typing "FAIL:$LIST"
  ax key $PID 125; sleep 0.5; ax key $PID 36; sleep 2
  if dialog 2; then
    [ "$(prefilled)" = bob ] && $AX dump $PID 40 | grep -q -E 'AXTextField \| Passwort \| •+' \
      && record auth04_keyboard_select PASS || record auth04_keyboard_select "FAIL:$(prefilled)"
    [ "$(prefilled)" = bob ] || pick bob   # keep the journey going
    ax press $PID "AXButton:Anmelden"
    waittitle "auth:bob@Ahoi Realm A:$A" 15 && record auth04_signin PASS || record auth04_signin FAIL
  else
    # Return both chose the entry and submitted the dialog: only a bob
    # sign-in proves the keyboard selection.
    echo "-- dialog closed by Return" >> "$OUT/steps.txt"
    if waittitle "auth:bob@Ahoi Realm A:$A" 15; then record auth04_keyboard_select PASS; record auth04_signin PASS
    else record auth04_keyboard_select FAIL:dialog-closed; record auth04_signin FAIL; fi
  fi
else
  record auth04_filter_by_typing FAIL:no-dialog; record auth04_keyboard_select FAIL:no-dialog
  record auth04_signin FAIL:no-dialog
fi
# 3c AUTH-14: the signed-in credential is sent preemptively inside /a/ only,
#    never to a path outside the protection space. AUTH-13: a redirect to
#    another origin carries no Authorization header.
settle; goto "http://127.0.0.1:$A/a/sub/observe?k=inside"; wait_seen inside 15
echo "preemptive inside /a/: $(seen inside)" >> "$OUT/steps.txt"
goto "http://127.0.0.1:$A/pub/observe?k=outside"; wait_seen outside 15
rec auth14_no_preemptive_outside_path [ "$(seen outside)" = 0 ]
goto "http://127.0.0.1:$A/a/redirect-cross"
if wait_seen redirect-cross 20; then rec auth13_cross_origin_redirect_no_auth [ "$(seen redirect-cross)" = 0 ]
else dialog 2 && ax press $PID "AXButton:Abbrechen"; record auth13_cross_origin_redirect_no_auth FAIL:not-reached; fi
# 4 Realm separation: Realm B on the same origin offers neither account.
settle; goto "http://127.0.0.1:$A/b/"; challenge
$AX dump $PID 40 | grep -q 'Realm: Ahoi Realm B' && record realm_b_prompt PASS || record realm_b_prompt FAIL
LIST=$(accounts); [ -z "$LIST" ] && record realm_separation PASS || record realm_separation "FAIL:$LIST"
# 4b AUTH-18: "never save" for Realm B; a later explicit save is suppressed.
# An empty store proves nothing unless carol really signed in (build 50
# passed auth18_never_save_first although the dialog was already gone).
CAROL=0; login carol carol-pass-1 "$NEVER" && waittitle "auth:carol@Ahoi Realm B:$A" 15 && CAROL=1; sleep 3
rec auth18_never_save_first eval 'alive && [ $CAROL = 1 ] && ! store | grep -q "Ahoi Realm B"'
settle; command switch; dialog
has_ax "AXRadioButton \| $SAVE" && echo "-- never-save realm still shows the save option" >> "$OUT/steps.txt"
CAROL=0; login carol carol-pass-1 "$SAVE" && waittitle "auth:carol@Ahoi Realm B:$A" 15 && CAROL=1; sleep 3
rec auth18_suppresses_save eval 'alive && [ $CAROL = 1 ] && ! store | grep -q "Ahoi Realm B"'
# 5 Port separation: same realm name on another port offers no account. Then
#   sign in there once (not saved) so step 7 can prove the switch clears only
#   the active origin's cache.
settle; goto "http://127.0.0.1:$B2/a/"; challenge
LIST=$(accounts); [ -z "$LIST" ] && record port_separation PASS || record port_separation "FAIL:$LIST"
login alice alice-pass-1 ""
wait_rt ":$B2/a/" "auth:alice@Ahoi Realm A:$B2" 15 && record b2_signed_in PASS || record b2_signed_in FAIL
# 6 Password update: server rotates alice; old saved password is rejected
#   without deleting the account; the new one updates the same row.
settle; curl -s http://127.0.0.1:$A/__rotate >/dev/null
goto "http://127.0.0.1:$A/a/"; sleep 3
command switch; dialog
pick alice; ax press $PID "AXButton:Anmelden"
dialog 30 && record rejected_reprompt PASS || record rejected_reprompt FAIL
rec auth06_error_text has_ax "$FAILED_TEXT"
echo "$(store)" | grep -q "|alice" && record rejected_keeps_account PASS || record rejected_keeps_account FAIL
login alice alice-pass-2 "$UPDATE"
waittitle "auth:alice@Ahoi Realm A:$A" 15 && record password_update_signin PASS || record password_update_signin FAIL
[ "$(store)" = "http://127.0.0.1:$A/Ahoi Realm A|alice http://127.0.0.1:$A/Ahoi Realm A|bob " ] \
  && record update_no_duplicate PASS || record update_no_duplicate "FAIL:$(store)"
# 6b AUTH-06: wrong password -> understandable error -> choose the other
#    saved account and sign in. AUTH-07: the failure changed nothing stored.
settle; command switch; dialog
login alice alice-wrong-9 ""
dialog 30; rec auth06_error_after_wrong_password has_ax "$FAILED_TEXT"
pick bob; ax press $PID "AXButton:Anmelden"
waittitle "auth:bob@Ahoi Realm A:$A" 15 && record auth06_switch_other_account PASS || record auth06_switch_other_account FAIL
[ "$(store)" = "http://127.0.0.1:$A/Ahoi Realm A|alice http://127.0.0.1:$A/Ahoi Realm A|bob " ] \
  && record auth07_after_second_failure PASS || record auth07_after_second_failure "FAIL:$(store)"
# 7 Sign out without restart: switch, then cancel the challenge -> 401 page.
settle; command switch; dialog && ax press $PID "AXButton:Abbrechen"
sleep 5; SIGNED=$(curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys
p=[t for t in json.load(sys.stdin) if t["type"]=="page" and t["url"].endswith(":"+sys.argv[1]+"/a/")]
print(p[0]["title"] if p else "<no realm-A tab>")' "$A")
# Signed out means the page is no longer authenticated: after the switch the
# reload is challenged again and Cancel leaves either the 401 page or, since
# the connections were closed and the 401 is no-store, an empty document
# (build 33). Either way no "auth:<user>" page may remain.
if [ "$SIGNED" != "<no realm-A tab>" ] && [ "${SIGNED#auth:}" = "$SIGNED" ]; then record sign_out_without_restart PASS
  echo "after switch+cancel: $SIGNED" >> "$OUT/steps.txt"
else
  # Build 32 showed only the URL as title: record what the tab shows.
  curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys
for t in json.load(sys.stdin):
    if t["type"]=="page": print("page:", t["url"], "|", t["title"])' >> "$OUT/steps.txt"
  node "$S/cdp.mjs" $PORT "127.0.0.1:$A" Runtime.evaluate '{"expression":"document.body?document.body.innerText.slice(0,200):\"<no body>\"","returnByValue":true}' >> "$OUT/steps.txt" 2>&1
  record sign_out_without_restart "FAIL:$(title)"
fi
kill -0 $PID 2>/dev/null && record same_browser_process PASS || record same_browser_process FAIL
# AUTH-16: signing out kept both saved accounts.
[ "$(store)" = "http://127.0.0.1:$A/Ahoi Realm A|alice http://127.0.0.1:$A/Ahoi Realm A|bob " ] \
  && record auth16_signout_keeps_saved PASS || record auth16_signout_keeps_saved "FAIL:$(store)"
# AUTH-15: only the active origin's cache was cleared; the port-B2 tab is still
# signed in after a reload.
eval_in ":$B2/a/" "location.reload()"; sleep 1
wait_rt ":$B2/a/" "auth:alice@Ahoi Realm A:$B2" 15 && record auth15_other_origin_kept PASS || record auth15_other_origin_kept FAIL
# 7b Sign out in a Workspace with its own website sessions: its tabs use their
#    own StoragePartition, so the switch must reset that partition's auth
#    cache, not the profile default's (4a11c52).
# Escape goes straight to the process: an HID Escape arrives asynchronously
# and would close the menu just opened by AX.
wsmenu() { # <active workspace> <item regex>
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  for attempt in 1 2 3 4; do
    ax press $PID "$1, Workspace wechseln" AXShowMenu
    waitax "AXMenuItem \\| $2" 4 && return 0
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 2
  done; return 1; }
KUNDE_OK=0
settle
if wsmenu Inbox "Neuer Workspace…"; then
  ax press $PID "Neuer Workspace…"
  if waitax "AXTextField \\| Workspace-Name" 8; then
    ax setvalue $PID "Workspace-Name" "Kunde"; sleep 1
    ax press $PID "AXRadioButton:Eigene Website-Sitzungen"; sleep 1
    ax press $PID "Erstellen"; waitax "Kunde, Workspace wechseln" 10 && KUNDE_OK=1
    # The create dialog must be gone, or it keeps key focus.
    end=$(( $(date +%s) + 6 ))
    while [ $(date +%s) -lt $end ] && $AX dump $PID 14 | grep -q "AXButton | Erstellen"; do sleep 1; done
  fi
fi
if [ $KUNDE_OK = 1 ]; then
  # Isolation: the default partition is still signed in on port B2; the
  # Workspace's own partition must challenge instead of reusing that cache.
  sleep 2; goto "http://127.0.0.1:$B2/a/?ws=kunde-b2"
  if dialog 20; then record own_sessions_isolated PASS; ax press $PID "AXButton:Abbrechen"; sleep 2
  else record own_sessions_isolated "FAIL:$(realm_title ws=kunde-b2)"; fi
  goto "http://127.0.0.1:$A/a/?ws=kunde"; challenge && login bob bob-pass-1 ""
  end=$(( $(date +%s) + 15 )); while [ $(date +%s) -lt $end ] && [ "$(realm_title ws=kunde)" != "auth:bob@Ahoi Realm A:$A" ]; do sleep 1; done
  [ "$(realm_title ws=kunde)" = "auth:bob@Ahoi Realm A:$A" ] && record own_sessions_signin PASS || record own_sessions_signin "FAIL:$(realm_title ws=kunde)"
  command switch; dialog && ax press $PID "AXButton:Abbrechen"
  sleep 5; KT=$(realm_title ws=kunde); echo "own sessions after switch+cancel: $KT" >> "$OUT/steps.txt"
  # An empty title means the browser is gone (build 40 crash), not signed out.
  alive && [ -n "$KT" ] && [ "$KT" != "<no tab>" ] && [ "${KT#auth:}" = "$KT" ] \
    && record own_sessions_sign_out PASS || record own_sessions_sign_out "FAIL:$KT"
  wsmenu Kunde "Inbox" && ax press $PID "$($AX dump $PID 14 | grep -oE 'AXMenuItem \| Inbox( – [^|]*)? \|' | head -1 | sed -E 's/^AXMenuItem \| //; s/ \|$//')"
  waitax "Inbox, Workspace wechseln" 8 || echo "-- could not switch back to Inbox" >> "$OUT/steps.txt"
else
  $AX dump $PID 14 > "$OUT/ax-kunde-failure.txt"; record own_sessions_signin FAIL:workspace-not-created
fi
alive && record phase1_browser_alive PASS || record phase1_browser_alive FAIL

# ---- Phase 2: full quit and relaunch of the same profile (AUTH-02) ---------
quit; sleep 2
launch restart
alive && record restart_same_profile PASS || record restart_same_profile FAIL
# R0 AUTH-14: the saved Realm-A accounts belong to /a/; the same realm name
#    on /z/ gets no automatic offer (the network auth cache is empty now).
goto "http://127.0.0.1:$A/z/?r=z"; challenge
LIST=$(accounts); [ -z "$LIST" ] && record auth14_chooser_path_scoped PASS || record auth14_chooser_path_scoped "FAIL:$LIST"
ax press $PID "AXButton:Abbrechen"; sleep 2
# R1 AUTH-26: a same-origin protected image prompts with its own realm and
#    origin but without stored-account or save controls. Chromium blocks only
#    cross-site subresource prompts, and the port is not part of the site: a
#    same-site image on another port may prompt, but only naming its own
#    origin; a cross-site image (localhost from 127.0.0.1) gets no prompt. The
#    fixture's k= marker proves each image request really arrived.
settle; goto "http://127.0.0.1:$A/pub/img.html?r=sub"
if dialog 20; then
  $AX dump $PID 40 > "$OUT/ax-subresource-dialog.txt"
  rec auth26_same_origin_prompt_labelled eval 'has_ax "Realm: Ahoi Subresource" && has_ax "127\.0\.0\.1:$A"'
  rec auth26_no_persistence_controls eval 'no_ax "AXRadioButton \| $SAVE" && no_ax "$USE_SAVED"'
  ax press $PID "AXButton:Abbrechen"; sleep 2
else record auth26_same_origin_prompt_labelled FAIL:no-dialog; record auth26_no_persistence_controls FAIL:no-dialog; fi
settle; goto "http://127.0.0.1:$A/pub/img-cross.html?r=subx"; wait_seen subx-port 15
if dialog 5; then
  $AX dump $PID 40 > "$OUT/ax-subresource-cross-port-dialog.txt"
  rec auth26_cross_port_prompt_labelled eval 'has_ax "Realm: Ahoi Subresource" && has_ax "127\.0\.0\.1:$B2" && no_ax "AXRadioButton \| $SAVE" && no_ax "$USE_SAVED"'
  ax press $PID "AXButton:Abbrechen"; sleep 2
else
  rec auth26_cross_port_prompt_labelled eval '[ "$(seen subx-port)" = 0 ] && [ "$(realm_title r=subx)" = subres-cross ]'
fi
settle; goto "http://127.0.0.1:$A/pub/img-site.html?r=subs"; wait_seen subx-site 15; sleep 5
rec auth26_cross_site_no_prompt eval '[ "$(seen subx-site)" = 0 ] && [ "$(realm_title r=subs)" = subres-site ] && no_ax "AXHeading \| Anmelden"'
dialog 1 && ax press $PID "AXButton:Abbrechen"
# R2 AUTH-20: Digest challenge with the same realm name as the Basic accounts
#    offers none of them, signs in and saves as its own Digest entry.
settle; goto "http://127.0.0.1:$A/a/dg/?r=dg"; challenge
rec auth20_digest_prompt eval 'has_ax "Realm: Ahoi Realm A" && $AX dump $PID 40 | grep -q -i "Authentifizierung: digest"'
LIST=$(accounts); [ -z "$LIST" ] && record auth20_digest_no_basic_accounts PASS || record auth20_digest_no_basic_accounts "FAIL:$LIST"
login dave dave-digest-1 "$SAVE"
wait_rt "r=dg" "digest:dave@Ahoi Realm A:$A" 15 && record auth20_digest_signin PASS || record auth20_digest_signin FAIL
sleep 3; DG=$(store_q 2); echo "digest store: $DG" >> "$OUT/steps.txt"
rec auth20_digest_saved_separately eval 'echo "$DG" | grep -q "Ahoi Realm A|dave" && ! store | grep -q "|dave"'
# R3 AUTH-02 + AUTH-19 + AUTH-05: the saved accounts are offered after the
#    restart, prefilled but never submitted automatically over HTTP; making
#    the other account preferred preselects it next time.
settle; goto "http://127.0.0.1:$A/a/?r=2"; challenge
PRE1=$(prefilled); echo "prefilled after restart: $PRE1" >> "$OUT/steps.txt"
rec auth05_prefilled_after_restart [ -n "$PRE1" ]
sleep 5; rec auth19_no_auto_login eval 'has_ax "AXHeading \| Anmelden" && has_ax "$HTTP_WARNING" && ! realm_title r=2 | grep -q "^auth:"'
LIST=$(accounts); [ "$LIST" = "alice bob " ] && record auth02_accounts_offered PASS || record auth02_accounts_offered "FAIL:$LIST"
echo "$LIST" | grep -q dave && record auth20_basic_not_mixed "FAIL:$LIST" || record auth20_basic_not_mixed PASS
OTHER=alice; [ "$PRE1" = alice ] && OTHER=bob
pick $OTHER; ax press $PID "AXButton:Als bevorzugt festlegen"; sleep 1
ax press $PID "AXButton:Abbrechen"; sleep 2
ax activate $PID; ax key $PID 15 cmd; dialog 20
[ "$(prefilled)" = "$OTHER" ] && record auth05_preferred_first PASS || record auth05_preferred_first "FAIL:$(prefilled)"
# AUTH-02/AUTH-08: the saved (updated) alice password signs in after restart.
pick alice; ax press $PID "AXButton:Anmelden"
wait_rt "r=2" "auth:alice@Ahoi Realm A:$A" 15 && record auth02_signin_saved PASS || record auth02_signin_saved FAIL
rec auth08_update_persisted grep -q -x "auth02_signin_saved PASS" "$OUT/results.txt"
# R4 AUTH-21/22 incognito: no automatic offer and no save options; a saved
#    account only after "use saved account"; nothing written; closing the last
#    incognito window drops its auth cache.
settle; BEFORE=$(store)
ax key $PID 45 cmd shift; sleep 3
goto "http://127.0.0.1:$A/a/?inc=1"; challenge
$AX dump $PID 40 > "$OUT/ax-incognito-dialog.txt"
rec auth21_no_automatic_fill eval '[ -z "$(prefilled)" ] && no_ax "AXRadioButton \| $SAVE" && has_ax "$USE_SAVED"'
ax press $PID "AXButton:$USE_SAVED"; sleep 2
pick alice; ax press $PID "AXButton:Anmelden"
wait_rt "inc=1" "auth:alice@Ahoi Realm A:$A" 15 && record auth21_explicit_selection PASS || record auth21_explicit_selection FAIL
sleep 3; rec auth21_no_persistence eval 'alive && [ -n "$BEFORE" ] && [ "$(store)" = "$BEFORE" ]'
ax key $PID 13 cmd shift; sleep 3
[ "$(realm_title inc=1)" = "<no tab>" ] && record incognito_window_closed PASS || record incognito_window_closed FAIL
ax key $PID 45 cmd shift; sleep 3
goto "http://127.0.0.1:$A/a/?inc=2"
if dialog 20; then rec auth22_cache_discarded eval '[ "$(realm_title inc=2)" != "auth:alice@Ahoi Realm A:$A" ]'; ax press $PID "AXButton:Abbrechen"
else record auth22_cache_discarded "FAIL:$(realm_title inc=2)"; fi
sleep 1; ax key $PID 13 cmd shift; sleep 3
# R5 AUTH-17: delete one saved account in the dialog (two steps); it is gone
#    from the store and from the next chooser.
settle; command switch; dialog
pick bob; ax press $PID "AXButton:Gespeichertes Konto löschen"; sleep 1
ax press $PID "AXButton:Löschen des Kontos bestätigen"; sleep 3
[ "$(store)" = "http://127.0.0.1:$A/Ahoi Realm A|alice " ] && record auth17_single_delete PASS || record auth17_single_delete "FAIL:$(store)"
LIST=$(accounts); [ "$LIST" = "alice " ] && record auth17_deleted_not_offered PASS || record auth17_deleted_not_offered "FAIL:$LIST"
pick alice; ax press $PID "AXButton:Anmelden"; wait_rt "r=2" "auth:alice@Ahoi Realm A:$A" 15
# R6 Credential manager. AUTH-18: the never-save realm is listed and can be
#    reset. AUTH-24: editing (the way to show/copy/hide a password) opens the
#    macOS system authentication; the journey only records that the prompt
#    appears, cancels it and checks that nothing was revealed.
sysauth_pids() { for n in coreautha SecurityAgent LocalAuthenticationRemoteService LocalAuthenticationUIService; do pgrep -x "$n"; done 2>/dev/null; }
sysauth_prompt() { # prints the pid whose AX tree shows the prompt
  local p; for p in $(sysauth_pids) $PID; do
    AHOI_AX_VALUE_MAX=300 $AX dump $p 14 2>/dev/null | grep -q -E "HTTP-Zugangsdaten zuzugreifen|Touch ID" && { echo $p; return 0; }
  done; return 1; }
# The manager is window-modal (SetModalType kWindow): on macOS a sheet.
settle; command manage
if waitax "AX(Window|Sheet) \| HTTP-Zugänge" 10; then
  record auth24_manager_opened PASS
  $AX dump $PID 40 > "$OUT/ax-manager.txt"
  rec auth18_listed_in_manager has_ax "Speichern deaktiviert: .*Ahoi Realm B"
  ax press $PID "AXButton:Speichern wieder erlauben"
  rec auth18_reset waitax "Speichern ist für diesen Realm wieder erlaubt" 8
  PRE_PIDS=$(sysauth_pids | tr '\n' ' ')
  ax press $PID "AXButton:Konto bearbeiten"
  PP=""; end=$(( $(date +%s) + 15 ))
  while [ $(date +%s) -lt $end ]; do PP=$(sysauth_prompt) && break; sleep 1; done
  NEW_PIDS=$(sysauth_pids | tr '\n' ' ')
  echo "system auth processes before: $PRE_PIDS after: $NEW_PIDS prompt pid: $PP" >> "$OUT/steps.txt"
  if [ -n "$PP" ]; then
    record auth24_system_prompt_shown PASS
    AHOI_AX_VALUE_MAX=300 $AX dump $PP 14 > "$OUT/ax-system-auth-prompt.txt" 2>&1
    # Cancel only: press the prompt's own Cancel; if AX cannot reach it, close
    # the manager, which invalidates the pending authentication.
    $AX press $PP "AXButton:Abbrechen" >> "$OUT/steps.txt" 2>&1; sleep 3
    sysauth_prompt >/dev/null && { ax press $PID "AXButton:Abbrechen"; sleep 3; }
    GONE=1; for i in 1 2 3 4 5; do sysauth_prompt >/dev/null && GONE=0 || { GONE=1; break; }; sleep 2; done
    [ $GONE = 1 ] && record auth24_prompt_cancelled PASS || record auth24_prompt_cancelled FAIL
  elif [ "$PRE_PIDS" != "$NEW_PIDS" ]; then
    # A new authentication process but no readable prompt: record, then cancel
    # through the manager.
    record auth24_system_prompt_shown "FAIL:process-only($NEW_PIDS)"
    ax press $PID "AXButton:Abbrechen"; sleep 3; record auth24_prompt_cancelled FAIL:unverified
  else
    record auth24_system_prompt_shown FAIL; record auth24_prompt_cancelled FAIL:no-prompt
  fi
  $AX dump $PID 40 > "$OUT/ax-manager-after-cancel.txt"
  # Fail closed: no editor, no revealed password; the status explains it if
  # the manager is still open.
  rec auth24_fail_closed eval 'alive && no_ax "AXStaticText \| [a-z]+ bearbeiten" && no_ax "Passwort ausblenden" && { no_ax "AX(Window|Sheet) \| HTTP-Zugänge" || has_ax "Die Authentifizierung wurde nicht abgeschlossen"; }'
  has_ax "AX(Window|Sheet) \| HTTP-Zugänge" && ax press $PID "AXButton:Abbrechen"; sleep 2
else
  $AX dump $PID 14 > "$OUT/ax-manager-failure.txt"; record auth24_manager_opened FAIL
fi
# R7 AUTH-18: after the reset a save for Realm B is stored again (carol was
#    not stored before, or the save proves nothing).
settle; R7_BEFORE=$(store)
goto "http://127.0.0.1:$A/b/?r=6"; challenge && login carol carol-pass-1 "$SAVE"
wait_rt "r=6" "auth:carol@Ahoi Realm B:$A" 15; sleep 3
rec auth18_save_after_reset eval '! echo "$R7_BEFORE" | grep -q "Ahoi Realm B|carol" && store | grep -q "Ahoi Realm B|carol"'
# 8 Forget this realm: saved accounts for Realm A are removed (Realm B stays).
#   Sign in through a fresh dialog first so the tab has an active realm.
settle; goto "http://127.0.0.1:$A/a/?r=8"
if ! challenge 15; then command switch; dialog; fi
login bob bob-pass-1 ""
wait_rt "r=8" "auth:bob@Ahoi Realm A:$A" 15
command forget; sleep 4
store | grep -q "Ahoi Realm A|" && record forget_realm "FAIL:$(store)" || record forget_realm PASS
rec forget_keeps_other_realm eval 'store | grep -q "Ahoi Realm B|carol"'
echo "digest store after forget: $(store_q 2)" >> "$OUT/steps.txt"
# AUTH-17: the reload after forgetting shows an empty dialog.
if dialog 20; then
  rec auth17_empty_after_forget eval '[ -z "$(prefilled)" ] && [ -z "$(accounts)" ]'
  ax press $PID "AXButton:Abbrechen"
else record auth17_empty_after_forget FAIL:no-dialog; fi
# 9 No password or Basic token in logs.
if grep -a -q -E 'alice-pass|bob-pass|YWxpY2U6|Ym9iOm' "$OUT/browser.log" "$OUT/fixture.log"; then
  record no_secret_in_logs FAIL; else record no_secret_in_logs PASS; fi
quit

# ---- Phase 3: proxy versus server credentials (AUTH-11), fresh profile -----
# Loopback traffic normally bypasses a proxy; <-loopback> routes it through
# the fixture proxy. A fresh profile keeps restored tabs out of this phase.
STORE_P=$(mktemp -d /private/tmp/ahoi-auth-proxy-profile.XXXXXX)
launch proxy --proxy-server=http://127.0.0.1:$PX "--proxy-bypass-list=<-loopback>"
goto "http://127.0.0.1:$A/pub/observe?k=proxy"
if dialog 30; then
  $AX dump $PID 40 > "$OUT/ax-proxy-dialog.txt"
  rec auth11_proxy_prompt has_ax "127\.0\.0\.1:$PX"
  login pia pia-proxy-1 "$SAVE"
else record auth11_proxy_prompt FAIL:no-dialog; fi
wait_rt "k=proxy" "public:proxy" 20 && record auth11_proxy_signin PASS || record auth11_proxy_signin FAIL
settle; goto "http://127.0.0.1:$A/a/?px=1"; challenge
LIST=$(accounts); [ -z "$LIST" ] && ! has_ax "127\.0\.0\.1:$PX" \
  && record auth11_origin_prompt_separate PASS || record auth11_origin_prompt_separate "FAIL:$LIST"
login alice alice-pass-2 "$SAVE"
wait_rt "px=1" "auth:alice@Ahoi Realm A:$A" 15; sleep 3
PS=$(store); echo "proxy-phase store: $PS" >> "$OUT/steps.txt"
rec auth11_store_separate eval 'echo "$PS" | grep -q "127.0.0.1:$PX/Ahoi Proxy|pia" && echo "$PS" | grep -q "http://127.0.0.1:$A/Ahoi Realm A|alice" && [ "$(echo "$PS" | grep -o "|pia" | wc -l | tr -d " ")" = 1 ]'
curl -s http://127.0.0.1:$A/__proxy-report > "$OUT/proxy-report.json"
rec auth11_headers_separate python3 -c 'import json,sys
r=json.load(open(sys.argv[1]))
sys.exit(0 if r["forwarded"]>0 and r["proxy_header_with_origin_creds"]==0 and r["origin_saw_proxy_header"]==0 and r["origin_saw_proxy_creds"]==0 else 1)' "$OUT/proxy-report.json"
quit; sleep 2

# ---- AUTH-25: value-blind scan after the visible journey -------------------
# Canaries: every synthetic password and every Basic token; they live next to
# the profile (never in $OUT) and are deleted on exit. The scan writes file
# names only.
python3 - > "$P-canaries" <<'PY'
import base64
pairs = [("alice", "alice-pass-1"), ("alice", "alice-pass-2"), ("alice", "alice-wrong-0"),
         ("alice", "alice-wrong-9"), ("bob", "bob-pass-1"), ("carol", "carol-pass-1"),
         ("dave", "dave-digest-1"), ("pia", "pia-proxy-1"), ("sam", "sam-pass-1")]
for user, password in pairs:
    print(password)
    print(base64.b64encode(f"{user}:{password}".encode()).decode())
PY
: > "$OUT/secret-scan.txt"
scan() { # <label> <paths...>
  local label=$1; shift; local hits
  hits=$(grep -rlaF -f "$P-canaries" "$@" 2>/dev/null)
  echo "$label: ${hits:-none}" >> "$OUT/secret-scan.txt"; [ -z "$hits" ]; }
rec auth25_no_canary_in_evidence scan evidence "$OUT"
rec auth25_no_canary_in_profile scan profiles "$P" "$STORE_P"
CRASHES=$(find "$HOME/Library/Logs/DiagnosticReports" -newer "$P-start" -name 'AhoiBrowser*' 2>/dev/null)
echo "crash reports since start: ${CRASHES:-none}" >> "$OUT/secret-scan.txt"
if [ -n "$CRASHES" ]; then rec auth25_no_canary_in_crash_reports scan crash-reports $CRASHES
else record auth25_no_canary_in_crash_reports PASS; fi
HDR=$(grep -laE '(Proxy-)?Authorization: (Basic|Digest) [A-Za-z0-9+/=]{6}|Digest username=' "$OUT"/browser.log "$OUT"/netlog-*.json "$OUT/fixture.log" 2>/dev/null)
echo "full Authorization headers: ${HDR:-none}" >> "$OUT/secret-scan.txt"
rec auth25_no_auth_header_in_logs [ -z "$HDR" ]
rm -f "$P-canaries"

# ---- AUTH case verdicts -----------------------------------------------------
all_pass() { local s; for s in "$@"; do grep -q -x "$s PASS" "$OUT/results.txt" || { echo false; return; }; done; echo true; }
# AUTH-01: new Basic credentials are saved only after a successful sign-in.
auth01_save_after_success=$(all_pass auth01_not_saved_before_success save_first)
# AUTH-02: after a full quit and relaunch the saved accounts are offered and work.
auth02_offered_after_restart=$(all_pass restart_same_profile auth02_accounts_offered auth02_signin_saved)
# AUTH-03: two accounts of one realm are saved and visibly selectable.
auth03_two_accounts_selectable=$(all_pass save_second store_two_accounts choice_lists_both choose_account)
# AUTH-04: username found by typing and selected with the keyboard.
auth04_keyboard_autocomplete=$(all_pass auth04_filter_by_typing auth04_keyboard_select auth04_signin)
# AUTH-05: the preferred account is preselected (last-successful prefill recorded).
auth05_preferred_preselected=$(all_pass auth05_prefilled_after_restart auth05_preferred_first)
# AUTH-06: wrong password shows an understandable error; another account works.
auth06_error_then_other_account=$(all_pass auth06_error_text auth06_error_after_wrong_password auth06_switch_other_account)
# AUTH-07: a single failure deletes nothing saved.
auth07_failure_keeps_saved=$(all_pass rejected_reprompt rejected_keeps_account auth07_after_second_failure)
# AUTH-08: a changed password is updated only after the successful login.
auth08_update_after_success=$(all_pass password_update_signin update_no_duplicate auth08_update_persisted)
# AUTH-09: two realms of one host stay separate.
auth09_realms_separate=$(all_pass realm_b_prompt realm_separation)
# AUTH-10: the same realm name on another port stays separate.
auth10_ports_separate=$(all_pass port_separation)
# AUTH-11: proxy and server credentials stay separate (prompt, store, headers).
auth11_proxy_server_separate=$(all_pass auth11_proxy_prompt auth11_proxy_signin auth11_origin_prompt_separate auth11_store_separate auth11_headers_separate)
# AUTH-13: a cross-origin redirect receives no Authorization header.
auth13_redirect_no_authorization=$(all_pass auth13_cross_origin_redirect_no_auth)
# AUTH-14: path/protection-space rules prevent too broad reuse.
auth14_protection_space_scoped=$(all_pass auth14_no_preemptive_outside_path auth14_chooser_path_scoped)
# AUTH-15: switching clears the right cache (default and own-sessions
# partition, only the active origin), reloads and shows the chooser.
auth15_switch_right_cache=$(all_pass auth15_switch_shows_chooser sign_out_without_restart same_browser_process b2_signed_in auth15_other_origin_kept own_sessions_isolated own_sessions_signin own_sessions_sign_out)
# AUTH-16: signing out keeps the saved accounts.
auth16_signout_keeps_saved=$(all_pass auth16_signout_keeps_saved)
# AUTH-17: a deleted account is not offered; after forgetting the realm the next dialog is empty.
auth17_delete_leaves_empty_dialog=$(all_pass auth17_single_delete auth17_deleted_not_offered forget_realm forget_keeps_other_realm auth17_empty_after_forget)
# AUTH-18: never-save suppresses saving and can be reset (credential manager).
auth18_never_save_resettable=$(all_pass auth18_never_save_first auth18_suppresses_save auth18_listed_in_manager auth18_reset auth18_save_after_reset)
# AUTH-19: HTTP shows a clear warning and never signs in automatically.
auth19_http_warning_no_autologin=$(all_pass auth19_http_warning auth19_no_auto_login)
# AUTH-20: Digest works and never mixes with Basic entries.
auth20_digest_not_mixed=$(all_pass auth20_digest_prompt auth20_digest_no_basic_accounts auth20_digest_signin auth20_digest_saved_separately auth20_basic_not_mixed)
# AUTH-21: incognito uses a saved account only explicitly and saves nothing.
auth21_incognito_explicit_only=$(all_pass auth21_no_automatic_fill auth21_explicit_selection auth21_no_persistence)
# AUTH-22: closing the last incognito window discards its auth cache.
auth22_incognito_cache_dropped=$(all_pass incognito_window_closed auth22_cache_discarded)
# AUTH-24 (partial, assisted): password access needs system authentication;
# the prompt appears and cancelling it reveals nothing.
auth24_system_auth_gate=$(all_pass auth24_manager_opened auth24_system_prompt_shown auth24_prompt_cancelled auth24_fail_closed)
# AUTH-25: no canary or full auth header in logs, NetLog, crash reports, evidence or profiles.
auth25_no_secret_anywhere=$(all_pass no_secret_in_logs auth25_no_canary_in_evidence auth25_no_canary_in_profile auth25_no_canary_in_crash_reports auth25_no_auth_header_in_logs)
# AUTH-26: subresource prompts are unambiguous (same-site ones name their own
# origin) and cross-site ones are blocked.
auth26_subresource_prompt_clear=$(all_pass auth26_same_origin_prompt_labelled auth26_no_persistence_controls auth26_cross_port_prompt_labelled auth26_cross_site_no_prompt)
# AUTH-27: the full visible journey on the installed build.
auth27_full_journey=true
for v in "$auth01_save_after_success" "$auth02_offered_after_restart" "$auth03_two_accounts_selectable" \
  "$auth04_keyboard_autocomplete" "$auth06_error_then_other_account" "$auth08_update_after_success" \
  "$auth15_switch_right_cache" "$auth16_signout_keeps_saved"; do [ "$v" = true ] || auth27_full_journey=false; done

python3 - "$OUT/results.txt" \
  "AUTH-01=$auth01_save_after_success" "AUTH-02=$auth02_offered_after_restart" \
  "AUTH-03=$auth03_two_accounts_selectable" "AUTH-04=$auth04_keyboard_autocomplete" \
  "AUTH-05=$auth05_preferred_preselected" "AUTH-06=$auth06_error_then_other_account" \
  "AUTH-07=$auth07_failure_keeps_saved" "AUTH-08=$auth08_update_after_success" \
  "AUTH-09=$auth09_realms_separate" "AUTH-10=$auth10_ports_separate" \
  "AUTH-11=$auth11_proxy_server_separate" "AUTH-13=$auth13_redirect_no_authorization" \
  "AUTH-14=$auth14_protection_space_scoped" "AUTH-15=$auth15_switch_right_cache" \
  "AUTH-16=$auth16_signout_keeps_saved" "AUTH-17=$auth17_delete_leaves_empty_dialog" \
  "AUTH-18=$auth18_never_save_resettable" "AUTH-19=$auth19_http_warning_no_autologin" \
  "AUTH-20=$auth20_digest_not_mixed" "AUTH-21=$auth21_incognito_explicit_only" \
  "AUTH-22=$auth22_incognito_cache_dropped" "AUTH-24=$auth24_system_auth_gate" \
  "AUTH-25=$auth25_no_secret_anywhere" "AUTH-26=$auth26_subresource_prompt_clear" \
  "AUTH-27=$auth27_full_journey" > "$OUT/results.json" <<'PY'
import json, os, sys
rows = [l.split(" ", 1) for l in open(sys.argv[1]).read().splitlines() if l]
res = {k: v for k, v in rows}
cases = {k: v == "true" for k, v in (a.split("=", 1) for a in sys.argv[2:])}
reloads_file = os.path.join(os.path.dirname(sys.argv[1]), "reloads.txt")
reloads = sum(1 for _ in open(reloads_file)) if os.path.exists(reloads_file) else 0
not_drivable = {
    "AUTH-12": "needs an HTTPS origin with a trusted certificate; trusting one changes the "
               "login keychain and insecure certificate flags are ruled out by the fixture contract",
    "AUTH-23": "assisted: Mac B, CloudKit and iOS absence cannot be observed on this Mac",
    "AUTH-24": "partial: show, copy and hide need a passed system authentication, which the "
               "journey never attempts; only the prompt and a fail-closed cancel are proven",
}
print(json.dumps({"pass": all(v == "PASS" for v in res.values()) and len(res) >= 16
                  and all(cases.values()),
                  "harnessReloads": reloads, "cases": cases, "notDrivable": not_drivable,
                  "steps": res}, indent=1))
PY
cat "$OUT/results.json"
