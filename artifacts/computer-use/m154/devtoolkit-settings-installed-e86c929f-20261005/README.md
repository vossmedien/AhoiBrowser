# Devtoolkit 7/7 and settings sections 2/2 on installed e86c929f — 5 October 2026

Installed `/Applications/AhoiBrowser.app`, source `e86c929f` (Chromium
154.0.8037.93), MacbookPro2026.local. Harness at `db3fd727`
(`tools/desktop_e2e/devtoolkit-journey.sh`, `settings-sections-journey.sh`),
CDP only, fresh disposable profiles, own e2e.lock; started after two CPU
samples of 52/54 % idle and 602 s HID idle (`wait.log`).

| Journey | Result | Build 59 (`d3ebc1b9`, 1 Oct) |
| --- | --- | --- |
| devtoolkit (DEV-01 reload + restart, 04, 05, 09, 14, 15) | **7/7 PASS** | 4/7: DEV-09, DEV-15, DEV-01 after restart failed |
| settings-sections (link routing card, shortcut editor) | **2/2 PASS** | 0/2 (probe timeout harness) |

The DEV fixes between build 59 and e86 are the native tab-cache policy,
document/worker factory binding, temporary header backend and the
request-mode correction `92facb58`. Visible panel journeys (editor UI)
remain separate.
