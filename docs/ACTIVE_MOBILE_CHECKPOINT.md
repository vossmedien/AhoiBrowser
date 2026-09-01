# Active Mobile checkpoint

Last updated: 2026-09-01

This is the restart and compaction checkpoint for the active Mobile work. Do
not replace this boundary with another inventory or optional feature package.
The complete target remains
`outputs/AhoiBrowser-Mobile-Final-Abschluss-Zielprompt.md`.

## Ownership

- Work only in `apps/AhoiMobile`, Mobile-only fixtures/tests, Mobile outputs,
  and this checkpoint.
- Do not stage or edit the active Desktop Arc, AnyChat, or uBlock files.
- uBlock on Mobile remains a feasibility report only until the core closure
  matrix is green.

## Exact current boundary

- Branch: `codex/desktop-core-feature-wave-20260830`
- Last committed Mobile candidate: `552b2f1d05beff33613d128ee982332f7cf9339c`
- iPhone simulator: `CAE7F82B-52D2-4607-992C-EDF40C323DE3`
- Derived data: `/private/tmp/ahoi-mobile-dd.QIp4oT`
- Fixture: `/private/tmp/ahoi-mobile-final-e2e.2L5D04/fixture-iphone`
- Candidate receipts: `/private/tmp/ahoi-mobile-evidence.7GFGFR`
- Background recovery and memory-warning recovery are visibly green.
- The external-URL regression is visibly green on an earlier exact candidate
  and must be repeated once on the final candidate.
- The visible Sync projection no longer crashes in `CKContainer` after the
  internal DEBUG transport split. Its first rerun reached Settings, then failed
  because the UI test scrolled the application instead of the Settings form.

## Locked execution order

1. Build the Settings-form accessibility correction and rerun only the failed
   visible Sync navigation journey on the exact new candidate.
2. Run the remaining visible Sync privacy and conflict journeys serially.
3. Close the bounded download package: same-process retry, private relaunch
   exclusion, authenticated-cookie transfer and interrupted-normal recovery.
   Do not persist request URLs, cookies, headers, or opaque resume blobs.
4. Run the remaining visible iPhone closure matrix, then iPad and accessibility
   journeys. Repeat the external-URL canary on the final candidate.
5. Only after visible E2E is green, run the programmatic, build, signing,
   security, performance and evidence gates.
6. Attempt external Apple/device/CloudKit/TestFlight gates last and report each
   unavailable human, account, hardware or grant boundary exactly.
7. Commit and push only owned Mobile changes. Do not force-push.

For each red visible journey, make one bounded correction and rerun that same
journey first. If a real external or hardware boundary remains, record it and
continue with independent work instead of opening a new feature investigation.
