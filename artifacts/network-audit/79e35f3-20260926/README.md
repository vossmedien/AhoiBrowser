# H5 fresh-profile network audit – build 39 (`79e35f3`)

Installed build 39, source `79e35f3b…`, executable SHA-256 `26913c40824d…`
(full values in `audit.json`). Run 2026-09-26 03:32–03:44 UTC after the
owner's journeys (`e2e39.done`), with no `e2e.lock` held and `h3.lock` held.
Same tool and method as builds 31–33 (`eb9277f`). This is the first
candidate with the strict-privacy proxy (068, patch 0066) and the GPC renderer
preference (070, patches 0067/0068); the default mode used by this audit
must be unaffected by both.

| Check (`docs/NETWORK_SILENCE_CHECKLIST.md`) | Result |
| --- | --- |
| NET-GCM-01 | **PASS**: no GCM hosts, no `GCM Store` directory |
| Fresh-profile silence | **PASS**: no denied or unknown host |

Hosts: `update.googleapis.com` ×130, `edgedl.me.gvt1.com` ×121,
`safebrowsing.googleapis.com` ×15, all allowlisted. `lsof` saw three peers on
port 443. The result matches builds 31–33: the privacy changes add no
background traffic.

The `--log-net-log` "unsupported command-line flag" bar came from the audit.
