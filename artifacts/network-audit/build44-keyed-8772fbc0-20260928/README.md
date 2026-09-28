# H5 keyed fresh-profile audit — installed build 44 (`8772fbc0`)

Crest lane, 28 September 2026, 16:45–16:57 CEST, user-authorized, after the
owner's build-44 journey chain finished; `h3.lock` held, no build/E2E lock.
`fresh_profile_audit.py --lease --phases idle,navigation
--google-api-key-from-keychain` (key only in the browser environment; the
NetLog shows it as `REDACTED…`, no `AIza…` value present). Binary SHA-256
prefix `09de503b12b7`, source `8772fbc0`, which contains the 094 decision
`1374b17b` (Translate off until the user enables it).

| Check | Verdict |
| --- | --- |
| NET-GCM-01 | **PASS** |
| Fresh-profile silence | **PASS** |
| PRIV-14 (Safe Browsing lists load; `threatListUpdates:fetch` HTTP 200) | **PASS** |
| PRIV-12 (navigation endpoints vs allowlist) | **PASS** |

Hosts: `update.googleapis.com` (150), `edgedl.me.gvt1.com` (123),
`safebrowsing.googleapis.com` (16), navigation `example.com`.
`translate.googleapis.com` appears **0 times** in the NetLog — the build-40
keyed failure (`../build40-keyed-20260926-1115/`, Translate language list on
the first public navigation) is resolved on this candidate.
