#!/usr/bin/env python3
"""Loopback-only HTTP cache counters for the installed tab-cache journey."""

import argparse
import collections
import http.server
import json
import threading
import urllib.parse


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, port, log_path):
        super().__init__(("127.0.0.1", port), Handler)
        self.log_path = log_path
        self.counts = collections.Counter()
        self.guard = threading.Lock()


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        cache = "no-store"
        kind = "text/html; charset=utf-8"
        if url.path in ("/a.html", "/b.html", "/solo.html"):
            title = {"/a.html": "PaneA", "/b.html": "PaneB",
                     "/solo.html": "Solo"}[url.path]
            suffix = {"/a.html": "A", "/b.html": "B", "/solo.html": "Solo"}[url.path]
            body = (f"<!doctype html><title>{title}</title><h1>{title}</h1>"
                    "<label>Synthetischer Entwurf "
                    f"<input id=f aria-label='Cache-Test-Entwurf-{suffix}'></label>")
        elif url.path == "/cache":
            tag = urllib.parse.parse_qs(url.query).get("tag", [""])[0]
            if tag not in ("shared", "worker", "restored"):
                self.send_error(400)
                return
            # Never reflect arbitrary incoming headers or credentials.
            raw = self.headers.get("X-Ahoi-Dev")
            marker = "configured" if raw == "configured" else (
                "missing" if raw is None else "unexpected")
            with self.server.guard:
                self.server.counts[tag] += 1
                count = self.server.counts[tag]
                with open(self.server.log_path, "a") as log:
                    log.write(json.dumps({"tag": tag, "count": count,
                                          "requestHeader": marker}) + "\n")
            body = json.dumps({"count": count, "requestHeader": marker})
            kind = "application/json"
            cache = "public, max-age=3600"
        elif url.path == "/worker.js":
            kind = "application/javascript"
            body = """
              onmessage = async event => {
                const results = [];
                for (let i = 0; i < 2; i++) {
                  const response = await fetch('/cache?tag=worker');
                  results.push({...await response.json(),
                    responseHeader: response.headers.get('X-Ahoi-Resp')});
                }
                postMessage(results);
              };
            """
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--log", required=True)
    args = parser.parse_args()
    Server(args.port, args.log).serve_forever()


if __name__ == "__main__":
    main()
