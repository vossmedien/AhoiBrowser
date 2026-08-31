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

## uBlock Origin Classic

AhoiBrowser contains no broad built-in ad blocker. Dogfood builds expose one
manual initial-install candidate for the **Official GitHub release** 1.74.0.
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

This exception remains identity/version-bound and does not enable arbitrary or
unpacked MV2. Ordinary MV3, extension isolation, permission handling, and
non-uBO update behavior are unchanged. A separately provisioned signed catalog
is retained only for later updates; it is not a prerequisite for the pinned
initial check and is not currently enabled as a network trust root.

Installed-app tests for install, filtering, update, performance, restart,
incognito, tamper, malicious package, and uninstall remain release gates. The
static technical pins do not by themselves grant public redistribution rights.
