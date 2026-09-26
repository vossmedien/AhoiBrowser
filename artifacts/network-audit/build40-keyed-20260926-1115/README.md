# Keyed network audit on build 40 (`6acd207`)

PRIV-14 with a real Safe Browsing API key, as its own variant. The key only
works for the Safe Browsing API and comes from the login Keychain. It exists
only in the browser's environment and is replaced with `REDACTED-KEY` in the
kept NetLog; the key appears 0 times in the artifact.

Tool: `tools/network_audit/fresh_profile_audit.py` at `b9bf54e`, with
`--phases idle,navigation --google-api-key-from-keychain`. Run on 26 Sep,
11:15–11:27 CEST, on the installed build 40 with a disposable profile,
holding `h3.lock`. No build or e2e lock was held.

| Row | Result | Evidence |
| --- | --- | --- |
| PRIV-14 (lists load) | **PASS** | `v4/threatListUpdates:fetch` → HTTP 200 (keyless build 39: 400) |
| NET-GCM-01 | PASS | no GCM host, no GCM check-in |
| Shutdown | exited | `Browser.close` answered. The first keyed run at 10:59 hung: the browser ignored `Browser.close` for 60 s and SIGTERM for 30 s. That run left no verdict, and the tool now escalates to SIGKILL. |
| Fresh-profile silence / PRIV-12 | **FAIL** | `translate.googleapis.com` ×15, denied rule N3: `GET /translate_a/l?client=chrome&hl=de&key=…` → **200** |

The key enables Chromium's Translate language-list refresh, which is skipped
without a key. It runs on the first navigation, without any translate UI. The
API restriction does not stop it: the endpoint answers 200. Everything else
matches the keyless audits: component updater, its download server, and Safe
Browsing.

Decision needed, handoff 094: the N3 row of `docs/NETWORK_SILENCE_CHECKLIST.md`.

`netlog.json.gz`: full NetLog (Default capture: URLs and status lines, no
cookies or credentials; key redacted).
