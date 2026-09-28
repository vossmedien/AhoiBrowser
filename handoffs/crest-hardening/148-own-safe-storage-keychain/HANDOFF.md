# 148 — Own "Safe Storage" Keychain item for Ahoi (OSCrypt)

Status: ready (finding + design; no patch)
Owner lane: desktop
Source: [Crest Chromium build comparison](../../../docs/reviews/crest-hardening-2026-09-28-crest-chromium-build.md), candidate 1.

## Finding

Ahoi overrides neither the OSCrypt Keychain service nor its account, so it uses
Chromium's default `Chromium Safe Storage` / `Chromium`
(`components/os_crypt/common/keychain_password_mac.mm:41`; no match for
`Safe Storage` in `overlay/` or `patches/`). That item exists on this Mac
(`security find-generic-password -s "Chromium Safe Storage"`). Every other
Chromium build on the machine — including our own `upstream-release` control —
therefore shares Ahoi's cookie/password encryption key, subject only to the
Keychain ACL. Crest's Chromium host uses a per-bundle item
(`Patches/native-host.patch`, `keychain_password_mac.mm` hunk; MPL-2.0, adopt
the concept, not the file).

## Proposed change

- Service `Ahoi Safe Storage`, account `Ahoi` (or derived from the bundle id),
  via a small patch to `KeychainPassword` service/account names.
- **Migration:** a bare rename creates a new key, invalidating `Secure
  Preferences` MACs and resetting protected prefs (Crest lost all extensions
  this way). On first start with the new item missing and the old one
  present, copy the existing secret into the new item, then use only the new
  one; never delete the shared item (other Chromium builds may own it).
- Best done before launch while no user profiles exist; document it in the
  network/privacy checklist.

## Tests / acceptance

Unit: new service/account names; migration copies once and is idempotent;
fresh profile creates only the Ahoi item. Visible: existing dev profile keeps
cookies, saved passwords and extensions across the upgrade; a fresh profile
leaves `Chromium Safe Storage` untouched.
