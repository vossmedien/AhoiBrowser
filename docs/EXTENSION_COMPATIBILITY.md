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

AhoiBrowser contains no broad built-in ad blocker. If the legal and technical
gates pass, one signed, hash-pinned catalog entry may enable only uBlock Origin
Classic's required legacy Manifest V2 behaviors. The exception is keyed to an
explicit extension identity/version policy, has a dedicated updater and kill
switch, and does not re-enable arbitrary MV2 extensions. It must not weaken MV3,
extension isolation, or Web Store policy for other extensions.

Until the selective prototype passes install, update, filtering, performance,
restart, incognito, and malicious-package tests, the feature remains disabled
and the product makes no uBO Classic compatibility claim.
