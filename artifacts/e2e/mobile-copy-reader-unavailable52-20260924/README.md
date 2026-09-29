# Mobile page-link copy and unavailable Reader — 24 September 2026

Outcome: **one visible iOS 27 Simulator journey passed, zero failures or skips**
on exact Xcode 27.0 DebugLocal build52
(`testPageLinkCopiesAndUnavailableReaderKeepOriginalPage`).

On the loaded synthetic WebKit fixture page, the browser actions sheet's
"Adresse kopieren" put exactly `https://fixture.ahoibrowser.test/start` on the
general pasteboard, and "Link als Markdown kopieren" replaced it with exactly
`[Ahoi Fixture](<https://fixture.ahoibrowser.test/start>)`. The UI-test runner
read both values off the main thread and answered the system paste-consent
prompt ("Einsetzen erlauben"), so the check reads the real system pasteboard,
not an in-app mirror. The fixture's article was then removed in the live page;
"Lesemodus" showed the localized alert "Auf dieser Seite wurde kein lesbarer
Artikel gefunden.", no Reader content appeared, and after dismissal the
original page was still displayed. Both screenshots in `screenshots/` were
visually reviewed.

Diagnostic history: build51 (source `86293bc`) ran the same journey and failed
RED (EXIT65) because the runner's paste-consent helper waited for the label
"Einfügen erlauben"; the German Simulator prompt says "Einsetzen erlauben".
Source `bda2815` corrects only that test label. The first build51 attempt was
interrupted by a session restart before compiling finished; it produced no
result. Build51 logs/results stay local under `/private/tmp/ahoi-mobile-copy.Rv6SBM/`
and are not acceptance evidence.

Build52 is clean detached source `bda281542e98aad8b2e5c172e1ce7811e2622a3a`,
`DebugLocal` 0.1 (52), simulator arm64, ad-hoc signed. Deep signature
verification passed; built and post-test installed bundles were byte-for-byte
equal (`diff -qr` exit0) and both Info.plists report source `bda2815` and
build52. Hashes are in [candidate.json](../../build/mobile-copy-bda2815-20260924/candidate.json).
The Simulator (A168, iOS 27) was returned to Shutdown.

Limits: synthetic local fixture, not a public website; a normal (non-private)
tab only, so the private-tab `localOnly` pasteboard option and Universal
Clipboard exclusion are **not** proved. Titles needing Markdown escaping,
credential stripping in copied URLs, real-device behavior, Desktop behavior
and full DoD27 acceptance remain open. It is not a Sync result; C645/CloudKit
was untouched.
