#!/usr/bin/env python3
"""Parse chrome://process-internals text for the sandbox readback.

usage: process_internals_frames.py <process-internals.json> <site port> <results.txt>
Frame Trees rows read "Frame[<process>:<routing>]: ..., locked, site:<site> | url: <url>";
"locked" means the process may only host that site. Appends site_per_process and
cross_site_separate_processes (PASS, FAIL or NOT_MEASURED).
"""
import json, re, sys
text = json.load(open(sys.argv[1])).get("result", {}).get("value", "") or ""
port = sys.argv[2]
rows = re.findall(r"Frame\[(\d+):\d+\]: ([^|]*)\| url: (\S+)", text)
def row(host):
    for proc, attrs, url in rows:
        if url.startswith(f"http://{host}:{port}/"):
            return proc, attrs
    return None
a, b = row("127.0.0.1"), row("localhost")
out = open(sys.argv[3], "a")
if not (a and b):
    out.write("site_per_process NOT_MEASURED\ncross_site_separate_processes NOT_MEASURED\n")
else:
    locked = all("locked" in r[1] and "site:" in r[1] for r in (a, b))
    out.write(f"site_per_process {'PASS' if locked else 'FAIL'}\n")
    out.write(f"cross_site_separate_processes {'PASS' if a[0] != b[0] else 'FAIL'}\n")
