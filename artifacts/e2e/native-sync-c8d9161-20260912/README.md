# Native first Sync opt-in — 12 September 2026

Status: **PARTIAL / CHANGED JOURNEY BLOCKED**, not a CloudKit roundtrip pass.

Candidate: installed `c8d9161057cae20913ddd83bfbfadf22598d3950`, exact
Development scope `bba96b17-f044-4923-9d40-67b15014d59e`.
Installed executable SHA256:
`29118f340a5dbaceaee1c9164353bca6d9b8d9c57dc0ee17242df7b9e7402ee5`.
Build and guarded installation have their original receipts in
`artifacts/build/native-sync-c8d9161-20260912/` and
`artifacts/install/ahoi-dev-c8d9161-scoped-20260912.json`.

## Actual entry path and observations

- The installed app launched normally into the dedicated `MacA` acceptance
  profile, not the user's real Default. Initial own-window capture is
  `root-start.png` (SHA256
  `ecd84bb808c3ff769e9415a89464844cf66212d851996206247e3007a8ccc6b5`).
- Root opened the visible Settings gear, then the Settings navigation's
  AhoiBrowser section. Native macOS Accessibility `AXScrollToVisible` brought
  the actual Ahoi Sync checkbox into the captured viewport. It was enabled and
  OFF: `root-sync-control-off.png` (SHA256
  `8709bca18250a688378ff07525460682e2929e059a751c66a07b5abe094e71b2`).
- Root used that observed checkbox's ordinary `AXPress`. Its value became1;
  the screenshot shows CloudKit recognized and the enabled main switch.
  `root-sync-activated.png` (SHA256
  `b79112623b09fbb641c9149bb9c158ce611e8840a5f74d9a9e9f327c21f9d87a`).
  Settings simultaneously instructs the user to complete account/zone recovery.
- Native AX readback finds `Lokale Daten weiter hochladen` and
  `Ohne lokalen Upload fortfahren`, both enabled but size0x0. Their `Sync`
  disclosure has size0x28. The screenshot has no visible recovery controls.
  Root did not press these hidden elements. Common and Desktop were given
  separate concrete diagnostics for the first-account/recovery state and layout.
- Common's selective read-only flags at12:34:53UTC confirmed
  `accountTransitionPending=true`, `zoneRecoveryPending=false`,
  `bookmarkConsentRevoked=true`, `generation=0`. The first-account classification
  is being corrected against the already verified account identity; real account
  changes remain fail-closed. Desktop's6a2cd6f fixes the0px preferred width and
  opens the disclosure when recovery first appears, without granting consent.
  These source corrections have not yet passed the visible journey.
- No category consent, remote-control approval or iPhone Sync opt-in was made.
  Device24 remains OFF. No payload keys, tokens or domain payloads were read or
  copied. The test scope may now contain genuine setup state; its prepared name
  is no longer grounds for assuming it unused.

## Harness limits and preserved attempts

CUA inventory/direct binding repeatedly timed out. Native System Events
Accessibility and CoreGraphics own-window capture worked without changing
permissions or security settings. The own Ahoi window was ID172329 under
PID39235 when captured; these are historical observations, not future targeting
instructions. No foreign window was clicked or captured.

The first sidebar `Sync` AX press produced no visible change, and a PageDown
attempt did not scroll the Settings page. Those are not passes. A Cmd+L attempt
opened Ahoi's command palette; no URL was typed when the focus check failed,
and Escape closed it. Intermediate images retain these observations; only the
three explicit captures above support startup, visible OFF and actual opt-in.
`root-sync-current.png` is byte-identical to the ON capture, not another journey.

No programmatic regression suite was run in place of the failed product path.
Required next proof is a usable native setup/recovery path on its corrected
candidate, then the real matching device bootstrap and encrypted roundtrip.

## Cleanup and handback

Root pressed the visible main Sync checkbox back OFF and independently read
AX value0. `root-sync-cleanup-off.png` is byte-identical to the original visible
OFF capture. The normal application quit completed; a fresh process/window
check found PID39235 absent and0 windows. No profile/store/key was removed or
reset, and no recovery confirmation was made. Root explicitly handed the window
back to Desktop/Common and the external MBC owner (queue01a095a0). No current
Root UI reservation or automatic repeat remains. Source corrections proceed
without holding a shared Simulator or native focus slot.
