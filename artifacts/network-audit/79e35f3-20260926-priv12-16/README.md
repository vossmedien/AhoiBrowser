# PRIV-12 and PRIV-16 on build 39 (`79e35f3`)

Owner request (PRIV rows of the master contract). Tool
`tools/network_audit/fresh_profile_audit.py` at `96c831f`:
`--phases idle,navigation,crash --crash-browser`. Run 2026-09-26 05:20–05:34 UTC
on the installed build 39 with a disposable profile, holding `h3.lock`. No
`e2e.lock` or `build.lock` was held; the owner kept the window free.

| Row | Result | Evidence |
| --- | --- | --- |
| NET-GCM-01, PRIV-11/13 (idle) | PASS | only allowlisted background hosts (component update, Safe Browsing) |
| PRIV-12 (normal navigation, complete endpoint list) | **PASS** | loopback page plus `https://example.com/`. Endpoints: `update.googleapis.com` ×113, `edgedl.me.gvt1.com` ×106, `safebrowsing.googleapis.com` ×15 (allowlisted); `example.com` ×14 (navigated). Nothing denied or unknown. |
| PRIV-16 (controlled crash, no upload) | **PASS** | Renderer crash via `chrome://crash` (typed navigation), browser crash by SIGABRT to the tool's own browser process. 2 new reports in `Crashpad/pending/`; `settings.dat` uploads disabled; no `/cr/report` or crash host in the NetLog; no remote socket of `chrome_crashpad_handler`. |

Notes:

- The Crashpad database is the default one
  (`~/Library/Application Support/AhoiBrowser/Crashpad`), because Chromium
  ignores `--user-data-dir` for it. The two reports stay there as local files.
  With uploads disabled Chrome runs no upload thread, so they legitimately
  stay in `pending/`.
- `chrome://inducebrowsercrashforrealz` is not executed when opened through
  DevTools (not even as a typed `Page.navigate`), hence SIGABRT.
- `netlog.json.gz` is the full NetLog (Default capture: URLs and status lines,
  no cookies or credentials).
- The owner's short second browser at about 05:2x UTC could, at most, have
  made PRIV-16 stricter (a foreign crash handler); the result is PASS.
- PRIV-14 (Safe Browsing) is FAIL for a separate reason, documented in the
  lane checkpoint: the build has no API key, and the list request gets HTTP 400.
