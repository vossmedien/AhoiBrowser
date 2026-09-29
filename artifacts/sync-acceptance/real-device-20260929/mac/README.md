# Mac side of the real-device sync test — 29 Sep 2026

Candidate: installed build 50 `bdfcea08` copied, prepared, signed (Apple
Development) and verified with `prepare/verify-macos-cloudkit
--acceptance-scope` for scope `23855a90-ee61-499e-abed-bfdc52a881d7`, installed
with `development_installation.py --acceptance-scope` (receipt under
`artifacts/install/ahoi-dev-bdfcea08-cloudkit-scope-23855a90-*.json`).

1. 13:07 Ahoi Sync on → "iCloud-Accountwechsel benötigt Bestätigung";
   upload `lease_revoked`; the confirm buttons did nothing (root cause and fix
   `fb280c26`: any iCloud notification was treated as an account switch and
   the confirm path ignored the with-provider case).
2. 13:18 normal quit + relaunch (safe unblock: state was only in memory).
3. 13:20:05 `AhoiSyncUpload stage=ok expected=11 saved=6 ack=11`; Settings:
   **"Synchronisiert und bereit"** — the E2E key from the iPhone arrived via
   iCloud Keychain (the 23 Sep blocker).
4. **iPhone → Mac:** the Mac store contains the iPhone test tabs
   `ahoi-sync-ios-20260929T104849Z` and `…T105445Z` (`store-readback.txt`).
5. **Mac → iPhone:** 13:21:32 the Mac uploaded
   `https://example.com/?ahoi-sync-mac-20260929T112130Z`
   (`stage=ok saved=1 ack=1`); reception on the iPhone is being checked.
