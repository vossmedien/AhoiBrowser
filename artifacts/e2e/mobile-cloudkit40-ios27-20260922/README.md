# Mobile Development Sync check, 22 September 2026

Exact candidate: `CloudKitDevelopment` build40, source
`45330d2de8ff75e0d50bbbc88a4a3de9aaea8aa5`, Xcode27/iOS27, existing
fe842 Development scope. Candidate receipt and byte-identical preserved app:
`artifacts/build/mobile-cloudkit-url-policy-45330d2-20260922/`. Installed
bundle on owned C645 matched receipt tree/executable/Info.plist hashes.

The first normal Settings readback was **red**: Sync on, “iCloud-Zugriff
erforderlich”, key status disabled, manual Sync disabled, safe issue
`cloudkit-error:fetch:9`. Apple's iOS27 SDK identifies CloudKit error 9 as
`CKErrorNotAuthenticated`; the Apple Account page was visible, but its
verified-device list was unavailable. Screenshot
`01-icloud-access-required.png` SHA-256:
`28b9b3c64d04d1f46e538a10fe2f9bf23ed06229224dfd0f1da1fb4a199e2952`.

The user then entered the existing Apple Account password in the Simulator's
own Settings UI. After a normal Ahoi app restart, the same installed candidate
and scope visibly showed “Bereit”, “Verschlüsselung bereit” and
“Synchronisiert”. “Jetzt synchronisieren” was enabled; one normal tap returned
to the same synchronized state without a visible error. Screenshot
`02-ready-synchronized.png` SHA-256:
`17ef280b29bf903d207bde518ed13fd763ae9841f9c271d5f95ac965bca09542`.
The app was sent to Home to flush normal background state, then the exact C645
simulator was shut down. Sync was left enabled for the subsequent Mac peer test.
No password, Apple-account detail, key bytes or CloudKit record contents were
captured; only the bounded status and receipt/screenshot evidence above.

This proves a real Mobile40 CloudKitDevelopment provider-status journey on
iOS27 after authentication. It does not prove Mac/mobile record transport,
remote key delivery, push, Production or physical-device behavior. The initial
red state remains part of the evidence; the later pass does not erase it.
