#!/usr/bin/env python3
"""Loopback-only HTTP Basic-Auth fixture for the desktop AUTH journeys.

Synthetic accounts only. Two realms on one origin plus the same realm on a
second port, so saving, account choice, update, switching, logout and the
scheme/host/port/realm separation of the HTTP-auth gate can be observed.
Each protected page names the authenticated account in its <title> and body,
which the journey reads through Chromium DevTools.
"""
import argparse
import base64
import html
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ACCOUNTS = {
    "Ahoi Realm A": {"alice": "alice-pass-1", "bob": "bob-pass-1"},
    "Ahoi Realm B": {"carol": "carol-pass-1"},
}
# Updated passwords accepted after /__rotate (password-update journey).
ROTATED = {"alice": "alice-pass-2"}
ROUTES = {"/a/": "Ahoi Realm A", "/b/": "Ahoi Realm B"}
STATE = {"rotated": False}


class Handler(BaseHTTPRequestHandler):
    server_version = "AhoiAuthFixture/1"

    def log_message(self, fmt, *args):  # never log Authorization headers
        return

    def _send(self, code, title, body, extra=None):
        payload = (f"<!doctype html><title>{html.escape(title)}</title>"
                   f"<h1>{html.escape(body)}</h1>").encode()
        self.send_response(code)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        for key, value in (extra or {}).items():
            self.send_header(key, value)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path == "/__health":
            return self._send(200, "ok", "ok")
        if self.path == "/__rotate":
            STATE["rotated"] = True
            return self._send(200, "rotated", "alice password rotated")
        realm = next((r for p, r in ROUTES.items() if self.path.startswith(p)), None)
        if realm is None:
            return self._send(200, "Ahoi auth fixture", "public page")
        user = self._authenticated_user(realm)
        if user is None:
            return self._send(401, "Ahoi auth required", f"login required for {realm}",
                              {"WWW-Authenticate": f'Basic realm="{realm}", charset="UTF-8"'})
        port = self.server.server_address[1]
        return self._send(200, f"auth:{user}@{realm}:{port}", f"signed in as {user}")

    def _authenticated_user(self, realm):
        header = self.headers.get("Authorization", "")
        if not header.startswith("Basic "):
            return None
        try:
            user, _, password = base64.b64decode(header[6:]).decode().partition(":")
        except ValueError:
            return None
        expected = ACCOUNTS[realm].get(user)
        if STATE["rotated"] and user in ROTATED:
            expected = ROTATED[user]
        return user if expected is not None and password == expected else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8793)
    parser.add_argument("--second-port", type=int, default=8794)
    args = parser.parse_args()
    servers = [ThreadingHTTPServer(("127.0.0.1", p), Handler)
               for p in (args.port, args.second_port)]
    for server in servers[1:]:
        threading.Thread(target=server.serve_forever, daemon=True).start()
    print(f"ready http://127.0.0.1:{args.port} http://127.0.0.1:{args.second_port}", flush=True)
    servers[0].serve_forever()


if __name__ == "__main__":
    main()
