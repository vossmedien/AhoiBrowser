# e4 native Sync/sidebar journey — 2026-09-13

PARTIAL: visible entry/recovery controls worked; post-upload ACK crashed.
Installed source e4de9e14ca3073876e9aa17730876d15cfa41683, Development bba scope
SHA851600c142f1c289f5f878a23587eecbc9001a4e2c2e072f7c0e0f5dc13c9abe.
Build/sign/install terminal receipts: ../../build/native-sync-e4de9e1-recovery-20260913/
and ../../install/ahoi-dev-e4de9e1-scoped-20260913.json.

CUA bound /Applications/AhoiBrowser.app and ordinarily launched PID28427.
Startup Continue restored the existing Settings tab. Open-file readback bound
that process to the dedicated MacA path; no real Default files were opened.
Own-window screenshots and AX in the task show Gespeichert / Temporäre Tabs,
the corrected Sync ist ausgeschaltet status, and an actually visible OFF toggle.
CUA scroll returned noWindowsAvailable; native content focus + PageDown reached
the visible switch without another UI technology. Search/menu attempts did not
change Sync and are not extra acceptance evidence.

Visible Ahoi Sync changed0->1. The normally wide sidebar Sync disclosure showed
setup in progress, then the existing account recovery prerequisite. Both recovery
buttons were actually visible. The previously authorized Lokale Daten weiter
hochladen button was pressed using CUA AX. At00:44:06UTC selective readback was
accountTransitionPending=false, zoneRecoveryPending=false,
bookmarkConsentRevoked=true, generation=0. No flag/store/key reset occurred.

The next CUA observation unexpectedly rebound/launched PID50922 after PID28427
had crashed; it showed Chromium wurde nicht richtig beendet and a new tab.
That process was normally quit using the application menu and confirmed absent.
Sync remains enabled in the retained MacA profile; do not call it OFF or fresh.
There is no roundtrip or stable post-recovery UI pass. No category, remote-control
or Phone opt-in was made. No Ahoi UI reservation remains while fixing the crash.

## Exact crash

Original retained dump:
/Users/vossmedien/Library/Application Support/AhoiBrowser/Crashpad/pending/043378d6-103b-4bc9-b25b-1ecac602fe9b.dmp
SHA25685e0a26ebd77dd0b05cdfc0c2332fa8512f2c1510e3bf38dd23036ca5115cf3d.
No raw dump or memory payload was published or copied into source control.
Existing minidump_stackwalk/minidump_dump plus atos against the exact e4 AhoiDev
libraries identify Thread6, process uptime244s:

```
LOG_FATAL statement.cc:98:
Cannot call mutating statements on an invalid statement.
sql::Statement::CheckValid -> StepInternal -> Run
SyncStore::AcknowledgeOutbox (sync_store_acknowledgements.cc:49)
SyncPump::OnUploadFinished (sync_pump.cc:232)
```

Chromium's exact pin enables SQLITE_OMIT_UPSERT in
third_party/sqlite/sqlite_chromium_configuration_flags.gni:89. ACK used
INSERT…ON CONFLICT…DO UPDATE. The same unsupported syntax occurs in the native
tree observation receipt. Actual MacA table schemas exist; system SQLite can
prepare the SQL, so system-only SQL checks do not cover the Chromium build.
An earlier read-only schema check while the app was active returned database
locked, made no writes and was not retried until ordinary app shutdown.

Bounded source correction ccd24827be87ab2cdcc0e2579137eb7d83700b4b replaces the
two statements with supported INSERT OR REPLACE; ACK selects no row if the
existing receipt has a newer clock. Both tables have only the written receipt
columns and no foreign keys or triggers; transaction and error boundaries stay
unchanged. No schema/format/SQLite flag change or recovery reset.

Real-profile hashes remained identical through install and recovery:
Local State f276a820648630f742601ee961cdd3df93ad5e368ee785544afa82faf91ab5b1;
Default/Preferences fbe3d5665a16044872b22f8bb950ca20330f4aa39e6b0b10e1fda1864c6fe21e;
Default/Ahoi Tab Tree b633770009da0273eaddb37d92151c00b59971a57c6a031c6ef2e000a11cf26b.
