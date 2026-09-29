#!/usr/bin/env python3
"""Loopback fixture for devtoolkit-journey.sh.

Serves a page with a cacheable stylesheet and appends one JSON line per
request to --log: path, query and the request headers whose names start
with X-Ahoi. Synthetic values only.
"""
import argparse
import http.server
import json
import urllib.parse

PAGE = ("<title>devtoolkit</title>"
        "<link rel='stylesheet' href='/style.css?v=1'>"
        "<body><p id='p'>devtoolkit fixture</p></body>")


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        ahoi = {k: v for k, v in self.headers.items() if k.lower().startswith("x-ahoi")}
        with open(self.server.log_path, "a") as log:
            log.write(json.dumps({"path": url.path, "query": url.query,
                                  "headers": ahoi}) + "\n")
        if url.path == "/page":
            body, kind, cache = PAGE, "text/html; charset=utf-8", "no-store"
        elif url.path == "/style.css":
            # Cacheable on purpose: a second load must not reach the server
            # unless the toolkit's cache-off switch is active.
            body, kind, cache = "p{letter-spacing:1px}", "text/css", "max-age=3600"
        elif url.path == "/echo":
            body, kind, cache = json.dumps(ahoi), "application/json", "no-store"
        else:
            self.send_error(404)
            return
        data = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Cache-Control", cache)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--log", required=True)
    args = parser.parse_args()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.log_path = args.log
    server.serve_forever()


if __name__ == "__main__":
    main()
