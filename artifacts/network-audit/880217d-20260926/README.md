# H5 fresh-profile network audit – build 33 (`880217d`)

Installed build 33, source `880217d9…`, executable SHA-256 `6454c8c12397…`
(full values in `audit.json`). Run 2026-09-26 00:07–00:18 UTC, after the
owner's journey loop (`e2e33.done`) and without `e2e.lock`, holding
`h3.lock`. Same tool and method as builds 31/32 (`eb9277f`).

| Check (`docs/NETWORK_SILENCE_CHECKLIST.md`) | Result |
| --- | --- |
| NET-GCM-01 | **PASS**: no GCM hosts, no `GCM Store` directory |
| Fresh-profile silence | **PASS**: no denied or unknown host |

Hosts: `update.googleapis.com` ×108, `edgedl.me.gvt1.com` ×103,
`safebrowsing.googleapis.com` ×15, all allowlisted. Identical to build 32.

`lsof` additionally caught one socket to port 53 of the system's configured
DNS resolver (`scutil --dns`). That socket is Chromium's built-in DNS client
resolving the hosts above. It is not an extra destination, and the NetLog
shows no other hostname. Builds 31/32 did the same, but the 2 s polling did
not happen to catch the short-lived UDP socket.

The `--log-net-log` "unsupported command-line flag" bar came from the audit.
