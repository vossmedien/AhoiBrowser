# Extension menu zero-tab crash recovery

Date: 2026-08-30 (Europe/Belgrade)

Candidate:

- installed path: `/Applications/AhoiBrowser.app`
- executable SHA-256: `c56d2a23f214934973704a36d501ab77f5d2b9bf8475b9fb99df3420f8fdf9fe`
- development installation receipt: `artifacts/build/installed-ahoi-dev-c56d2a23f214-20260829T225353Z.json`
- receipt classification: development-only, not release-evidence eligible

Observed crash before the fix:

- report: `~/Library/Logs/DiagnosticReports/AhoiBrowser-2026-08-29-233407.ips`
- incident: `9C6FABD7-3DC5-48FD-B451-191125E393DA`
- crash path: `GetMainPageState` -> `ExtensionsMenuViewModel::GetOptionalSection` -> extension-menu open
- trigger: extension toolbar menu in a true Ahoi zero-tab window

Visible installed-app checks:

1. Started the installed candidate and dismissed the stale restore prompt.
2. Closed the only blank tab with Command-W, leaving the window open on `Leerer Ahoi-Arbeitsbereich`.
3. Opened and closed the extension menu four times in that zero-tab state.
4. Confirmed that 1Password and uBlock Origin Lite remain visible as generic extension entries while their page-bound primary actions are disabled.
5. Opened `https://example.com`, opened the menu, and confirmed that the normal site-permission controls and extension actions are available.
6. Quit the installed app normally, relaunched it, selected `Leer starten`, and opened the extension menu again in the restored zero-tab state.
7. Confirmed that the installed browser process remained alive and that no new `AhoiBrowser-*.ips` report appeared after installation at 2026-08-30 00:54 local time.

Evidence:

- `01-zero-tab-extension-menu-open.png`: first zero-tab menu state
- `02-https-extension-menu-open.png`: normal HTTPS menu state
- `03-restart-zero-tab-extension-menu-open.png`: zero-tab menu after a full app restart

Build boundary:

- Chromium compilation, linking, bundle staging, nested-code verification, signing, and runtime/resource verification completed successfully.
- The build wrapper intentionally returned exit 1 only when the provenance writer refused to issue an official build receipt for the dirty Ahoi repository.
- The canonical atomic development installer independently verified the candidate before staging, the same-volume copy, and the installed bundle after activation.
- This candidate therefore supports local installed-app E2E only and must not be represented as a release build.
