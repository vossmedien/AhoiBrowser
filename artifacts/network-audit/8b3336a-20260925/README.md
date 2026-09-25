# H5 fresh-profile network audit – build 31 (`8b3336a`)

Installed candidate `/Applications/AhoiBrowser.app`, build 31, source
`8b3336a70af9…`, executable SHA-256 `d637fd8c3b52…` (full values in
`audit.json`). Run 2026-09-25 21:06–21:17 UTC under the owner-confirmed
`installed-app` lease, holding `h3.lock`, with no `e2e.lock` (build 32 was
compiling under `build.lock`; the owner allowed that for this audit).

Tool: `tools/network_audit/fresh_profile_audit.py` at `eb9277f`. It uses a
disposable profile, Chromium NetLog (Default capture), `lsof` socket polling
every 2 s, no root, 600 s idle plus one local fixture page, and no UI input.

## Verdict against `docs/NETWORK_SILENCE_CHECKLIST.md`

| Check | Result |
| --- | --- |
| NET-GCM-01 (no GCM check-in/MCS, no GCM store) | **PASS**: no GCM hosts; no `GCM Store` directory was created |
| Fresh-profile silence (no denied or unknown host) | **PASS** |

Contacted hosts (all on the endpoint allowlist):

| Host | Events | Purpose |
| --- | --- | --- |
| `update.googleapis.com` | 128 | component updater |
| `edgedl.me.gvt1.com` | 115 | component downloads |
| `safebrowsing.googleapis.com` | 15 | Safe Browsing list updates |

Socket peers seen by `lsof`: four remote addresses on port 443, consistent
with the three hosts above. Compared with build 29 (`edced8d-20260925`),
`accounts.google.com` (ListAccounts, ×15) is gone; patch 0062 is effective.

## Notes for readers

- The audit window shows Chromium's "unsupported command-line flag" bar
  for `--log-net-log`. The audit caused it, and it sends nothing. It is not a
  product defect.
- Two earlier attempts at 23:01 and 23:03 CEST were void and deleted. The
  tool passed a symlinked bundle path, and the Mac sandbox crashed the
  browser's first child process. `eb9277f` fixed that.
- Limits: non-Chromium DNS (other processes) is not observed without root.
  An audit on build 32 or later only re-confirms this; H5 does not wait on it.
