# H5 fresh-profile network audit — installed build 40 (`6acd207`), keyless

Run by the Crest lane on 28 September 2026, 11:43–11:55 CEST, under the user's
explicit authorization (recorded in `docs/ACTIVE_CREST_HARDENING_LANE.md`),
holding `h3.lock` in the owner lock directory; no build/E2E lock was held.
Tool: `tools/network_audit/fresh_profile_audit.py --lease --phases
idle,navigation,crash`. Bundle binary SHA-256
`bc53748d319f1b086b03e25beceaea2e36d85e64dc0ca6916549de62ea743705`, Chromium
153.0.8010.53, `dev` profile, disposable user data directory, no API key.
Method: Chromium NetLog (Default capture) plus `lsof` socket polling every
2 s; no root, so non-Chromium DNS is not observed.

| Check | Verdict |
| --- | --- |
| NET-GCM-01 (no GCM check-in) | **PASS** |
| Fresh-profile silence (10 min idle + one local page) | **PASS** |
| PRIV-12 (navigation endpoints vs. allowlist) | **PASS** |
| PRIV-16 (renderer crash, no upload) | **PASS** — report stays pending, uploads disabled, no upload marker or crash-handler remote |
| PRIV-14 (Safe Browsing lists load) | **FAIL** — `v4/threatListUpdates:fetch` HTTP 400 without a key; this is the open owner decision PRIV-14, not a silence defect |

Contacted hosts, all allowlisted: `update.googleapis.com` (151),
`edgedl.me.gvt1.com` (138), `safebrowsing.googleapis.com` (16); navigation to
`example.com` only in the navigation phase. Files: `audit.json`,
`netlog.json.gz`, console log `../6acd207-20260928.log`. The keyed variant of
this build (`../build40-keyed-20260926-1115/`) still fails Translate silence
pending decision 094.
