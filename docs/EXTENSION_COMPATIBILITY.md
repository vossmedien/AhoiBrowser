# Extension compatibility

AhoiBrowser targets practical compatibility with Chromium extensions, including
Manifest V3, content scripts, service workers, DevTools extensions, permission
prompts, commands, side panels where supported by the Ahoi UI, native messaging,
and extension updates. Compatibility claims are extension/version-specific and
require installed-app tests.

## Password managers

1Password and Bitwarden are release-critical. Tests cover installation,
enable/disable, toolbar access, autofill, save/update login, locked/unlocked
behavior, browser restart, incognito opt-in behavior, and native messaging where
used. A missing vendor browser allowlist/signature/identity is reported as an
external integration gate, not simulated away.

## Distribution

Chrome Web Store branding, APIs, update URLs, and policies are Google-controlled
and must not be assumed available merely because Chromium accepts CRX packages.
The implementation will use only an authorized distribution/update path and
will document user-facing limitations. Enterprise policy and developer-mode
loading remain useful test paths but are not substitutes for consumer delivery.

## Extension commands and AnyChat

AnyChat is installed only through Chromium's normal Chrome Web Store flow.
AhoiBrowser does not download, repackage, pre-authorize, side-load, or provide
an AnyChat-specific installer. Chromium remains responsible for the extension
permission prompt, updates, enable/disable state, and removal.

Extension commands intentionally register as the sole high-priority handler
for their accelerator. Ahoi's additional browser-local shortcuts register as
normal-priority fallthrough handlers. While AnyChat
`khpefodpgnkegiohbolbaaeabnfdegln` is enabled, its `Command+Shift+S`
`toggle-sidebar` command therefore wins without colliding with Ahoi's own
sidebar shortcut. Once AnyChat unregisters that command, Ahoi's shortcut is
available again automatically.

Installed-app acceptance covers cancellation and successful installation from
the normal Web Store page, the standard permission prompt, extension action and
Side Panel use on HTTPS, disable/enable, browser restart, and absence of a new
crash. No custom AnyChat distribution path is part of that acceptance.

## uBlock Origin Classic

AhoiBrowser contains no broad built-in ad blocker. Every supported desktop
build exposes one manual initial-install candidate for the **Official GitHub
release** 1.74.0.
The signed browser pins release commit
`6dd2d95e50d134a477a4e183343c0b26e9147123`, complete CRX hash, CRX public-key
hash, and the key-derived identity `fkgkibajhfbepljeaefdnfnegdcjomkh`.
The initial metadata is compiled into AhoiBrowser, so opening the dialog causes
no catalog request.

The package download may start only at the exact pinned GitHub release URL. It
accepts at most one credentialless GitHub release-asset redirect, then verifies
the complete-file hash, CRX3 proof, public-key hash, derived ID, version, and
manifest before Chromium shows its normal permission prompt. Authorization is
committed only after installation succeeds.

The installer inventories three identities independently and always displays
their exact ID and runtime status before a destructive migration is possible:

- browser-pinned Official GitHub Classic:
  `fkgkibajhfbepljeaefdnfnegdcjomkh`;
- former Chrome Web Store Classic:
  `cjpalhdlnbpafiamejdnhcphjbkeiagm`;
- uBlock Origin Lite:
  `ddkjiahejlhfcafbddmgiahcphecmpfh`.

The initial Ahoi button is one action through download, verification, and the
handoff to Chromium's permission prompt. Cancel, download/verification failure,
or loss of the initiating tab cannot commit authorization and must discard the
temporary package. After verification, Ahoi automatically closes and detaches
its browser-modal sheet before it posts the installer handoff, so Chromium
never creates a second window-modal prompt underneath that sheet. Once the
handoff occurs, Chromium owns the permission prompt and its cancellation; sheet
destruction cannot cancel the accepted handoff. In Ahoi's intentional zero-tab
window, the same explicit button first creates a normal foreground Chromium tab
solely as the prompt host; it does not silently grant permissions or install
the extension.

The service owns one cancellable install operation through every asynchronous
installer outcome. Profile shutdown cancels that operation, clears pending
authorization without dereferencing a destroyed preference service, and
schedules deletion of the retained package even if Chromium never produces an
installer callback.

Installing Classic never disables or removes Lite. If Lite is present, Ahoi
records local-only migration state bound to the exact committed Classic
authorization and the current browser-process token. Lite remains installed
until Classic is enabled and runtime-ready, the entire browser has exited, a
later browser process has re-read and matched that authorization, and the user
then chooses the separately labelled **remove uBO Lite** action. This second
action is deliberate and never runs automatically after install success.
Malformed/mismatched state, failed persistence, a missing runtime-ready signal,
an unchanged process token, policy/uninstall refusal, or cleanup failure all
retain Lite. The former Classic identity is never silently migrated or removed.
Neither migration state nor Classic authorization is syncable.

This exception remains identity/version-bound and does not enable arbitrary or
unpacked MV2. Ordinary MV3, extension isolation, permission handling, and
non-uBO update behavior are unchanged. A separately provisioned signed catalog
is retained only for later updates; it is not a prerequisite for the pinned
initial check and is not currently enabled as a network trust root.

Installed-app tests for install, filtering, update, performance, restart,
incognito, tamper, malicious package, and uninstall remain release gates. The
static technical pins do not by themselves grant public redistribution rights.
