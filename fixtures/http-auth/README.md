# AhoiBrowser HTTP-auth fixture

This directory contains a local, dependency-free fixture for HTTP Basic and
Digest authentication. It is intentionally separate from browser production
code. Passing its self-tests proves the fixture contract only; it does **not**
prove AhoiBrowser credential UI, persistence, Keychain, incognito, sync, or
installed-app behavior.

## Requirements

- Python 3.9 or newer
- `openssl` on `PATH`
- loopback ports `18443`, `18444`, `19443`, `18080`, and `18081` available when using
  defaults

No certificate or private key is committed. Startup creates a two-day,
localhost-only P-256 certificate in the selected runtime directory. Graceful
shutdown removes the certificate, private key, OpenSSL config, and state file.
Redacted request and service logs remain for diagnosis.

## Lifecycle

From the repository root:

```sh
python3 fixtures/http-auth/manage.py start
python3 fixtures/http-auth/manage.py status
python3 fixtures/http-auth/manage.py print-config
python3 fixtures/http-auth/manage.py stop
```

`start` and `stop` are idempotent. Runtime state defaults to
`/tmp/ahoibrowser-http-auth-$UID`. Override it with `--state-dir`; override any
port with the corresponding `--*-port` option. The state JSON prints the exact
URLs and temporary certificate path. Synthetic credentials are printed only by
the explicit `print-config` command.

Run self-tests with:

```sh
python3 fixtures/http-auth/manage.py run-tests
```

The tests use random free ports and an isolated temporary directory. Their TLS
client accepts only the in-process, local fixture certificate. This does not
authorize AhoiBrowser E2E tests to use `--ignore-certificate-errors` or disable
certificate validation. Installed-browser tests must explicitly trust the
generated certificate through the test environment and remove that trust after
the run.

## Origins and routes

| Origin | Default | Purpose |
|---|---:|---|
| primary HTTPS | `https://127.0.0.1:18443` | two Basic and two Digest realms, same-/cross-origin redirects |
| secondary HTTPS | `https://127.0.0.1:18444` | same path on a different port and realm |
| cross HTTPS | `https://127.0.0.1:19443` | observes only whether an Authorization header arrived |
| explicit HTTP | `http://127.0.0.1:18080` | insecure Basic case carrying expected-warning metadata |
| synthetic proxy | `http://127.0.0.1:18081` | non-forwarding Basic proxy challenge with separate credentials |

Key routes:

- `/basic/alpha/resource`, `/basic/beta/resource`
- `/digest/alpha/resource`, `/digest/beta/resource`
- `/basic/alpha/redirect-same`, `/basic/alpha/redirect-cross`
- `/redirect/same-origin`, `/redirect/cross-origin`
- `/basic/plaintext/resource` on the HTTP origin
- `/subresource/page` and `/subresource/protected.svg`
- any request target at the synthetic proxy, challenged with `407`
- `/__fixture/health` on every origin
- `/__fixture/observer` on the cross-origin HTTPS target

Basic challenges include UTF-8 metadata. Digest challenges use RFC 7616 style
`SHA-256` with `qop=auth`, an opaque value, a per-run nonce, and nonce-count
replay rejection. All endpoints bind only to `127.0.0.1`.
The proxy fixture never forwards traffic; it exists to prove that
`Proxy-Authorization` is distinct from origin `Authorization` before the
installed-browser integration run.

## Logging and secret boundary

The fixture credentials are deliberately synthetic and repository-public. Do
not replace them with real values. Structured request logs contain method,
route-without-query, status, role, and whether authentication was present. An
Authorization header is serialized only as `[REDACTED]`; query strings are never
logged. Responses from the observer disclose only a boolean and never echo a
header value.

See [`../../docs/spikes/HTTP_AUTH_FIXTURE.md`](../../docs/spikes/HTTP_AUTH_FIXTURE.md)
for the AUTH-01…AUTH-27 coverage boundary.
