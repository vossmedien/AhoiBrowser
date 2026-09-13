# Combined provider/manual-retry candidate

Source26b01b17134a04e6d4e96bf9a837bbcf81ea06e3 =178 plus only the reviewed
post-success retry correction2948361 (SyncPump.cc,10 additions/1 deletion).
The frozen178 app-only build5910 completed EXIT0 before this snapshot advanced;
its receiptSHAba5a5eb9d7e60876e558a9314bc0df23961bcc7a5b2e75be9a73e0c7cc48819e
remains in ../native-sync-178dc7b-20260913/. No178 CloudKit installation occurred.

The actual successful Upload/Download commit must clear old backoff before a
coalesced automatic follow-up. A failed ClearRetry reports failure both there
and in ordinary FinishSuccess; callbacks are not prematurely declared successful.
There is no reset before any attempt. All provider/conflict/lease and explicit
manual-versus-automatic boundaries are those of the documented55/178 source.

Guarded app-only run65225,3jobs, start39%CPU idle/47GiB free; documented low-disk
warning with unchanged32GiB floor. Existing55 raw/entitled originals, installed
app, all rollbacks and actual MacA data/old02:45UTC retry evidence are preserved.
No extra tests, UI redesign, schema/wire/GN/engine roll, peer opt-in or reset.
