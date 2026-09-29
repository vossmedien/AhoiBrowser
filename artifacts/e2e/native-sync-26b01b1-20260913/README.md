# Successful retained MacA Sync recovery

Real local MacA upload/ACK, native receipts and first download verified.
No physical-peer or Mac–iOS roundtrip is claimed.

Candidate26b01b17134a04e6d4e96bf9a837bbcf81ea06e3, exact Development/bba scope
SHA851600c142f1c289f5f878a23587eecbc9001a4e2c2e072f7c0e0f5dc13c9abe.
Build65225, scoped prepare/sign/verify65919 and guarded install69712 EXIT0.
Original portable and entitled copies remain in ../../build/native-sync-26b01b1-20260913/.

- Build receipt SHAfe7ca07f38c5c28db8f188179af08990adaf3576bfcb723fbc6a95fcdd311001.
- CloudKit verification SHA3496149b298f231a23b00c64117cce8f195a17e4876606c183e31dc8f95f1311.
- Install receipt ../../install/ahoi-dev-26b01b1-scoped-20260913.json,
  SHAecaf11ad426826932cde7f46e64a2a6e3e82f439e41650b0ddaa7d622bca6dc3.
- Installed executable SHA8b21daa3b00c12e68886ebe1108b82f0fc4efc7e93fb68d8950814fdce0f00c9.

## Exact visible product journey

CUA bound /Applications/AhoiBrowser.app and ordinarily launched PID70398 at
approximately02:13UTC. Continue restored the retained MacA Settings page, Sync
already ON. The ordinary sidebar Sync disclosure was visible. Before the user
action there were no AhoiSyncUpload events; the saved automatic deadline02:45UTC
was still in the future. No store/pref/retry/key state was manually changed.

At02:13:59UTC the actual visible Jetzt synchronisieren button initiated a real
send before that deadline. Safe log stage7 reported CKErrorDomain2 with item14:
expected11/saved1/resolved5/unresolved5/ACK6. This incomplete send honestly
retained the provider_error status and outbox.

After that result, one justified explicit follow-up used the received server
metadata. At02:14:40UTC stage1 reported expected11/saved11/unresolved0/ACK11.
The same cycle then drained remaining pages5,1,1,1,4,1 with matching ACK counts,
no unresolved items and no errors. provider-events.json contains the eight exact
safe event rows for PID70398; no IDs, keys, payloads, tokens or raw NSError.

The next own-window CUA AX + screenshot showed Synchronisiert und bereit on
the same stable process. This is actual visible success, not a label-only code
change or a simulated provider. The failed first send remains in the evidence.

## Durable readback and cleanup

The normal application-menu Quit completed, then PID70398 was confirmed absent.
Read-only scoped SQLite at02:16UTC found:

```
outbox=0
acknowledged=16
native_observations=2
initial_fetch_complete=1
retry attempt=0, last_attempt=0, next_attempt=0, last_error=""
```

Selective existing inbox flags: accountTransitionPending=false,
zoneRecoveryPending=false, bookmarkConsentRevoked=true, generation2.
No new crashdump; the last one remains e4's preserved043378d6 dump. No category,
remote-control or Phone opt-in was made. The existing bba data is not a fresh
scope and must not be deleted/reset for the next peer journey. Sync remains ON
in the preserved MacA profile; no Ahoi process or own UI reservation remains.
Root received the explicit UI/checkout/build/sign/install handback.

Real Default hashes remain the same through all installs and visible journeys:
Local State f276a820648630f742601ee961cdd3df93ad5e368ee785544afa82faf91ab5b1;
Preferences fbe3d5665a16044872b22f8bb950ca20330f4aa39e6b0b10e1fda1864c6fe21e;
Ahoi Tab Tree b633770009da0273eaddb37d92151c00b59971a57c6a031c6ef2e000a11cf26b.

## Source and verification scope

Canonical commits614d297,2948361,628d163,f7d276c,6fee878 preserve the final
SQLite/provider/manual-retry behavior. A declaration-only merge conflict kept
the existing Workspace Structure methods and new SyncNowFromUser together.
Foreign WIP was not included or overwritten. The actual installed snapshot
remains26, not the larger canonical Structure/schema7 wave.

The previous narrow in-memory check used the actual Chromium SQLite dylib,
proved the old unsupported UPSERT fails and verified new monotonic ACK SQL.
Final runtime now independently exercises persisted ACK and native observation
receipts. Changed C++/Objective-C++ units compiled through the guarded app path;
no extra test target/matrix or mock transport was used as acceptance. Required
next proof is the genuine matching peer's UI/bootstrap/roundtrip. UX redesign
remains Root's separate pending ImageGen choice.
