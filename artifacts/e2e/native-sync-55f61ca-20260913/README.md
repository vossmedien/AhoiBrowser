# Provider conflict diagnostic journey

PARTIAL — no real ACK/roundtrip acceptance.
Exact installed55f61cae5b7c397860a6fe175065f3d97cd7097d, Development/bba scope
SHA851600c142f1c289f5f878a23587eecbc9001a4e2c2e072f7c0e0f5dc13c9abe.
Guarded build61547, scoped sign/verify21987 and guarded install69825 EXIT0.
Build receipt SHA8403f809bc56db315db576af48eeaf98871288cb9fecbcc6ff8469c5b1e8869b;
verification SHAc957132f9c3c7ff376495cdfc88c34e11b102092d81fda8682dc17ad67702d97;
install5133a643389be7db5b25fc67377c10685aad45e9253cfa86e2681a8ff5e5a174.
Installed executable e1a5802ec62a6e01e12d7dc5fecf145e234695b1282fc4e2abe4661e6c94a62e.

Ordinary CUA startup/Continue restored retained MacA Settings, Sync ON. PID94989
remained stable through opening the visible Sync disclosure and explicit Jetzt
synchronisieren actions. The original startup upload emitted this nonsecret
numeric diagnostic at01:40:10.643055UTC:

```
domain=none code=0 itemDomain=CKErrorDomain itemCode=14
expected=10 saved=1 resolved=5 unresolved=4 ack=6
```

Thus five actual record conflicts were resolved, one save succeeded and four
remain unresolved. The product correctly withheld a success/ACK for the incomplete
batch. The NSString stage was private in unified logs; no private value was read
or exposed. Item code14 is CKErrorServerRecordChanged. UI retained provider_error.

A visible follow-up attempt, justified by newly cached server record metadata,
produced no further send. Read-only source identified an additional exact cause:
SyncPump::StartCycle calls FinishFailure while merely waiting for next_attempt.
FinishFailure calls MarkRetry(now+NextRetryDelay), so each premature click and
the5-minute automatic timer moves the deadline again, up to a1-hour delay.
This can indefinitely starve retries; repeated UI clicks are not a remedy.

PID94989 was normally quit through its own menu and confirmed absent. No new
crashdump. At01:45:11UTC read-only retained store values were outbox16,
acknowledged0, native_observations0, retry attempt13/provider_error,
last_attempt01:45:00UTC, next_attempt02:45:00UTC. No retry flag, outbox, store,
key, zone, scope, category or remote-control consent was reset. Device24 remains
OFF under Root coordination. No UI reservation remains.

Minimal pump correction is prepared separately: waiting completes callbacks
without writing or advancing the existing retry state. Immediate manual retry
would need a distinct explicit UI intent and is not inferred from a hidden
database edit or automatic backoff bypass.
