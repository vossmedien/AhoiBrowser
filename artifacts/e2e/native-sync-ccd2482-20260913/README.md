# Retained MacA ACK follow-up — 2026-09-13

PARTIAL: stable visible candidate; real ACK/roundtrip remains unproven.
Candidate ccd24827be87ab2cdcc0e2579137eb7d83700b4b. Its exact two-file SQL fix
is canonical614d297868b50173d2b6ae8093c8da58ef5f1f55, DCO signed and pushed.
No Structure/schema7/UX proposal was integrated in this installed candidate.

Guarded build41122, scoped preparation/signing/verification14386 and guarded
install57636 completed EXIT0. Receipts:

- ../../build/native-sync-ccd2482-20260913/build-receipt.json,
  SHA3ad7f306987b090ee23e07d108f88e17a2a6875936303e6cb0b5f47b4e028160.
- ../../build/native-sync-ccd2482-20260913/cloudkit/verification.json,
  SHA4cdaf1052b7720d9a67d39f6f363a920bbe32346ed38cba5027882e32d131bda.
- ../../install/ahoi-dev-ccd2482-scoped-20260913.json,
  SHAd9ba2781fa920e67eea736049e482a02f3a1e780bbdde8e10dec0294058cff93.

Installed executable SHAc2ab46e0fa6ba0f04d87bf066dae3900d599ccba1bcc48d91724cd8498b15112.
Actual installed plist binds Development and the unchanged bba scope SHA
851600c142f1c289f5f878a23587eecbc9001a4e2c2e072f7c0e0f5dc13c9abe.
No Ahoi process was running before atomic installation; e4 is retained in rollback.

## Visible journey and actual result

CUA ordinarily launched PID41250 at approximately01:02:39UTC. The retained
predecessor crash marker offered session restore; the normal Restore button
restored the MacA Settings tab. No new ccd crashdump was created. Existing Sync
was ON, with accountTransitionPending=false and zoneRecoveryPending=false.
The sidebar's visible Sync disclosure showed Neuer Sync-Versuch geplant ·
provider_error. The actual visible Jetzt synchronisieren button was pressed
once. The same PID remained alive and responsive through subsequent own-window
AX/screenshots, with the same retry status. No category/remote-control/Phone
consent was given. Root keeps Device24 OFF.

The app was normally quit through its own application menu; PID41250 was then
absent. Read-only counts after quit: outbox13, acknowledged0,
native_observations0, retry attempt9 / provider_error. Before launch these were
outbox10 / acknowledged0 / native_observations0. Session restore produced further
local changes. No outbox or acknowledgement was deleted or manually written.
Thus process stability is observed, but execution of a successful real ACK is
NOT proven; do not claim transport or cross-device acceptance.

After that visible product boundary, check-sql.py ran once against the exact
built Chromium SQLite dylib with synthetic :memory: tables. EXIT0:

```
PASS: exact original UPSERT is rejected by Chromium SQLite
PASS: initial, stale, logical, device-tiebreak and equal ACK clocks
PASS: native observation receipt is replaced once
```

No test suite, additional build target or mock transport was used as acceptance.

## Narrow remaining diagnostic boundary

Existing macOS unified logs for Ahoi PID41250/com.apple.cloudkit, approximately
01:02:43–01:04:10UTC, contain three send cycles: two finished and one failed;
three fetch cycles all finished. The one public error is01:02:48.458923UTC,
Engine, format `%s failed sending changes for context %s: %@`, CKErrorDomain
Code2 (PartialFailure). Item errors are private; no nested codes are available
in ordinary logs. The corresponding cloudd query yielded no attributable public
item error. Only format templates, domain/code and counts were printed; no
payload, key, token, account identity or record name was exposed.

Source hypothesis, not live proof: HandleSent can acknowledge a matching/newer
server-record conflict, while CompleteUpload still reports the aggregate partial
failure and withholds all ACKs. A subsequent empty acknowledged batch also becomes
provider_error. The next diagnostic boundary is HandleSent/CompleteUpload and
the pump's empty-ACK versus failed-persistence branches; no third run or source
change was made on an unverified hypothesis. No recovery/zone/key reset is valid.

UI explicitly returned after ordinary quit. Installed ccd and all prior
rollback/source/evidence remain protected; MacA Sync is retained ON.
