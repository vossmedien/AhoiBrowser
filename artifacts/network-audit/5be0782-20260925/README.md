# H5 fresh-profile network audit – build 32 (`5be0782`)

Installed candidate build 32, source `5be07823f9cf…`, executable SHA-256
`a3ee55461db8…` (full values in `audit.json`). Run 2026-09-25 21:39–21:51
UTC right after installation and before the owner's journey loop, under the
confirmed `installed-app` lease, holding `h3.lock`; no `e2e.lock` or
`build.lock` was held. Same tool and method as build 31 (`eb9277f`):
disposable profile, NetLog, `lsof` polling every 2 s, no root, 600 s idle
plus one local page.

| Check (`docs/NETWORK_SILENCE_CHECKLIST.md`) | Result |
| --- | --- |
| NET-GCM-01 | **PASS**: no GCM hosts, no `GCM Store` directory |
| Fresh-profile silence | **PASS**: no denied or unknown host |

Hosts: `update.googleapis.com` ×108, `edgedl.me.gvt1.com` ×103,
`safebrowsing.googleapis.com` ×15, all allowlisted. `lsof` saw three
peers on port 443. The result matches build 31: ADR 0011 step 2 (044–054)
adds no background traffic.

The "unsupported command-line flag" bar for `--log-net-log` came from the
audit and is not a product defect.
