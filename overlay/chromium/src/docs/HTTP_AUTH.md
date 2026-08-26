# AhoiBrowser HTTP authentication

## Runtime design

AhoiBrowser keeps HTTP Basic and Digest credentials in Chromium's profile
`PasswordStoreInterface`. On macOS that follows Chromium's encrypted password
storage and Keychain integration; AhoiBrowser does not create a second secret
database. The profile preference `ahoi.http_auth.metadata` contains only a
version, origin, server/proxy target, scheme, realm, permitted paths, network
anonymization key, username, preferred flag, and last-success time.

The complete protection key is server/proxy target plus exact scheme, host,
effective port, Basic/Digest scheme, case-sensitive realm, RFC-style protection
path, and network anonymization key. HTTPS and HTTP never share a credential.
An unsupported scheme, malformed metadata version, opaque/non-persistable
partition key, or missing PasswordStore fails closed.

The native `LoginView` shows origin/port, realm, scheme, transport status, an
editable and filterable username combobox, every exact-space saved account, a
masked password, preferred-account and two-step delete actions, and the
choices use once, save/update after success, or never save for the realm. A
second `401` explains that the account was rejected but not deleted. Basic over
HTTP remains usable for local/legacy systems, displays a transmission warning,
never auto-submits, and is persisted only after the user explicitly selects
the save option and the subsequent navigation succeeds.

The existing extension-first auth routing in `HttpAuthCoordinator` remains
unchanged. No DOM injection or proprietary extension credential API is added,
so 1Password and Bitwarden continue using their normal web-form/native-
messaging paths.

## Save and failure semantics

`LoginHandler` transfers a requested save to `LoginTabHelper`; the password is
kept only in transient memory. `LoginTabHelper` writes through
`HttpAuthCredentialService` only after a committed 2xx/3xx response in the
same origin and protection path. A `401`/`407`, failed navigation, or foreign-
origin redirect discards the transient request. A single failure never removes
or mutates a saved account. Updating the password for the same username uses
PasswordStore's update operation; a different username creates another realm
account.

`Never save` suppresses future writes but does not hide an account that the
user saved earlier. Deleting one saved account and ending the current auth
session are separate APIs.

## Incognito

The keyed service is owned by the regular profile so an OTR dialog can offer a
one-shot read after an explicit button press. Automatic PasswordStore lookup is
disabled in OTR, and every add, update, metadata change, preferred change, and
delete request is rejected. Active session identity lives only in the tab's
`HttpAuthSessionController` and dies with the OTR `WebContents`.

## Switch, forget, and command surfaces

`HttpAuthSessionController::SwitchAccount()` executes this sequence. It can
derive the exact current HTTP(S) origin even when the tab inherited a warm
Chromium auth-cache entry and therefore did not show a fresh dialog:

1. `ClearHttpAuthCache` for exactly the active origin;
2. `CloseAllConnections` on that profile NetworkContext;
3. normal reload, which allows the server to challenge again.

It does not touch PasswordStore. `DeleteSavedCredential()` changes only one
stored account. `ForgetRealmAndSwitch()` explicitly deletes the exact realm and
then performs the cache/connection/reload sequence. These methods are the
narrow integration contract for the separately owned Site Panel and Command
Bar actions `HTTP-Anmeldung wechseln`, `HTTP-Anmeldung für diese Website
vergessen`, and `Gespeicherte HTTP-Zugänge verwalten`.

## Threat model and evidence rules

- Never log a password, decoded Basic token, Digest response, or complete
  Authorization header in application logs, NetLog evidence, crash keys,
  fixture receipts, screenshots, or test reports.
- Do not preempt an auth challenge with a credential for another origin,
  port, transport, realm, auth scheme, path, target, or partition key.
- Do not persist a submitted credential before the matching server response
  succeeds.
- Do not interpret `Never save` as permission to delete existing credentials.
- Password reveal in a future settings manager must use Chromium's existing
  device-reauthentication gate. There is no plaintext export contract.
- The global connection close is intentional because NetworkContext has no
  origin-filtered connection-close API; the auth-cache deletion itself remains
  exact-origin.

## Focused verification

```sh
buildtools/mac/gn check out/AhoiDev //ahoi/browser/http_auth:http_auth
buildtools/mac/gn check out/AhoiDev //chrome/browser/ui/login:impl
autoninja -C out/AhoiDev ahoi_http_auth_unittests
out/AhoiDev/ahoi_http_auth_unittests
python3 -m py_compile fixtures/http-auth/server.py
```

Use `fixtures/http-auth/README.md` for the loopback server. Runtime evidence
must name only fixture case, realm, transport, port, result, and receipt ID;
redact usernames and never capture a password field containing a value.
