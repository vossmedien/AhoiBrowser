#!/usr/bin/env python3

import html
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit


class FixtureHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        request = {
            "path": self.path,
            "headers": {key.lower(): value for key, value in self.headers.items()},
        }
        print(json.dumps(request, sort_keys=True), flush=True)

        split = urlsplit(self.path)
        title = (split.path.strip("/") or "home").replace("-", " ").title()
        payload = json.dumps(request, indent=2, sort_keys=True)
        body = (
            "<!doctype html><html><head>"
            f"<title>{html.escape(title)}</title>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<style>body{font:16px system-ui;margin:40px;background:#17212b;color:#eef4fa}"
            "main{max-width:780px;margin:auto;padding:32px;border-radius:24px;"
            "background:#22313f;box-shadow:0 20px 60px #0008}pre{white-space:pre-wrap}</style>"
            "</head><body><main>"
            f"<h1>{html.escape(title)}</h1><pre>{html.escape(payload)}</pre>"
            "</main></body></html>"
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        return


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", 8765), FixtureHandler).serve_forever()
