# HTTP Basic and Digest authentication

HTTP authentication is a first-class Chromium integration, not a page-level
password-form workaround. Network challenges remain in Chromium's
`HttpAuthManager`/`LoginHandler` flow and credentials are saved through the
Chromium password store backed by macOS Keychain facilities.

## Credential identity

Saved entries are selected by scheme, canonical host, effective port, realm, and
protection-space semantics. Multiple usernames may exist for one target. The
chooser shows matching accounts, preferred/last-used state, and enough target
context to prevent realm confusion without revealing passwords.

Users can submit once, save, update, decline for now, or never save for a target.
Plaintext HTTP receives a prominent warning and never auto-submits a saved
credential. Digest secrets are handled according to Chromium's supported model.

## Session controls

Site controls and the command bar can clear only the relevant in-memory HTTP-auth
cache, allowing switch-account/logout without deleting stored credentials.
A settings manager supports search, account labels, preferred-account changes,
target-scoped deletion, and an authenticated reveal path where technically
appropriate. Logs and diagnostics never contain usernames with passwords,
Authorization headers, or secret material.

Incognito can use an explicitly entered credential for that OTR session but does
not persist, auto-suggest from the normal store, or sync it. The AUTH-01 through
AUTH-27 matrix covers first save, multiple accounts, ports/realms, Basic/Digest,
cancel/failure/update, cache clear/switch/logout, HTTP warning, redirects,
incognito, restart, deletion, logging, localization, keyboard, and accessibility.
