#!/usr/bin/env python3
"""Loopback fixture for privacy-modes-journey.sh.

Serves one page set on 127.0.0.1 and localhost (two different sites on the
same port) and appends one JSON line per request to --log: path, query,
Sec-GPC, Referer and whether a cookie arrived. Synthetic values only.
"""
import argparse
import http.server
import json
import ssl
import threading
import urllib.parse

PAGES = {
    # First-party login: /login sets a session cookie, /whoami reads it.
    "/login": ("<title>logged in</title>ok", [("Set-Cookie", "session=fp; Path=/; SameSite=Lax")]),
    # Top page on site A that embeds site B and sends a full-URL referrer
    # policy, so Chromium would send the whole URL unless Ahoi reduces it.
    "/top": ("<meta name='referrer' content='unsafe-url'><title>top</title>"
             "<iframe id='f' src='http://localhost:{port}/frame'></iframe>"
             "<img src='http://localhost:{port}/pixel?from=top'>", []),
    # Cross-site frame: tries an unpartitioned third-party cookie.
    "/frame": ("<title>frame</title><script>"
               "document.cookie='tp=1; SameSite=None; Secure; Path=/';"
               "window.tp=document.cookie;</script>frame", []),
    "/pixel": ("", []),
    # HTTPS pair for PRIV-02/03: a cross-site frame that sets an
    # unpartitioned and a partitioned (CHIPS) third-party cookie.
    "/top3p": ("<title>top3p</title>"
               "<iframe id='f' src='https://localhost:{port}/frame3p'></iframe>", []),
    "/frame3p": ("<title>frame3p</title><script>"
                 "document.cookie='tp=1; SameSite=None; Secure; Path=/';"
                 "document.cookie='chip=1; SameSite=None; Secure; Path=/; Partitioned';"
                 "window.tp=document.cookie;</script>frame3p", []),
    "/landing": ("<title>landing</title>landing", []),
    "/ads": ("<title>ads</title>ads", []),
}


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        with open(self.server.log_path, "a") as log:
            log.write(json.dumps({
                "host": self.headers.get("Host", ""),
                "path": url.path,
                "query": url.query,
                "gpc": self.headers.get("Sec-GPC"),
                "referer": self.headers.get("Referer"),
                "cookie": bool(self.headers.get("Cookie")),
            }) + "\n")
        if url.path == "/whoami":
            body = f"<title>whoami {self.headers.get('Cookie', '')}</title>"
            headers = []
        elif url.path in PAGES:
            body, headers = PAGES[url.path]
            body = body.format(port=self.server.server_port)
        else:
            self.send_error(404)
            return
        data = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        for key, value in headers:
            self.send_header(key, value)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--https-port", type=int)
    parser.add_argument("--cert")
    parser.add_argument("--key")
    args = parser.parse_args()
    if args.https_port:
        secure = http.server.ThreadingHTTPServer(("127.0.0.1", args.https_port),
                                                 Handler)
        secure.log_path = args.log
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(args.cert, args.key)
        secure.socket = context.wrap_socket(secure.socket, server_side=True)
        threading.Thread(target=secure.serve_forever, daemon=True).start()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.log_path = args.log
    server.serve_forever()


if __name__ == "__main__":
    main()
