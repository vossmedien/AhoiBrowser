# HTTP-auth fixture spike and AUTH matrix

Status: fixture implemented; AhoiBrowser integration and installed-app E2E not
yet proven.

The local fixture at `fixtures/http-auth/` gives development, integration, and
Computer Use tests deterministic Basic/Digest challenges without external
accounts. It separates realms, paths, ports, origins, schemes, and redirect
targets. Its self-tests validate server behavior, redirect-client policy, and
fixture log redaction. They deliberately do not impersonate Chromium UI or
credential-store tests.

## Reproducible evidence command

```sh
python3 fixtures/http-auth/manage.py run-tests
```

Expected result: nine tests pass. This is `INTEGRATION` fixture evidence, not
`CU_E2E`. A later installed-app run must start the fixture, trust its temporary
certificate without insecure Chromium flags, exercise the visible browser, and
store screenshots/video/log assertions under the corresponding
`artifacts/e2e/<version>/<test-id>/` directories.

## AUTH-01 through AUTH-27 mapping

Legend:

- **Fixture-tested**: automated here and usable as a deterministic browser test
  dependency.
- **Fixture-ready**: the route exists, but the assertion belongs to AhoiBrowser.
- **Browser-only**: requires Chromium UI/profile/store/session/sync behavior and
  cannot truthfully pass in this fixture.

| ID | Fixture contribution | Remaining authoritative proof |
|---|---|---|
| AUTH-01 | **Fixture-tested:** unauthenticated Basic challenge, successful credentials, and failed credentials. | Visible submit-then-save behavior in installed AhoiBrowser. |
| AUTH-02 | **Fixture-ready:** stable target/realm across fixture restarts. | Full browser quit/relaunch and saved-account offer. |
| AUTH-03 | **Browser-only.** | Save and visibly select two accounts for one exact realm. |
| AUTH-04 | **Browser-only.** | Keyboard username autocomplete in native chooser. |
| AUTH-05 | **Browser-only.** | Preferred/last-successful account selection. |
| AUTH-06 | **Fixture-tested:** wrong Basic and Digest material returns a fresh 401 challenge. | Understandable error and account switch in visible chooser. |
| AUTH-07 | **Fixture-ready:** repeated failures are deterministic and do not mutate the server. | Prove one failure does not delete a browser-stored credential. |
| AUTH-08 | **Fixture-ready:** successful response can confirm a changed test credential once a mutable-password scenario is added to the run. | Password-store update only after successful login. |
| AUTH-09 | **Fixture-tested:** two Basic and two Digest realms share the primary host and remain distinct. | Credential chooser/store isolation in AhoiBrowser. |
| AUTH-10 | **Fixture-tested:** primary and secondary HTTPS origins expose the same path on different ports with distinct realms. | Entries remain separated by effective port in the browser store. |
| AUTH-11 | **Browser-only:** this fixture does not implement a proxy challenge. | Separate server and proxy credentials with a dedicated proxy fixture. |
| AUTH-12 | **Fixture-ready:** HTTPS and explicit HTTP origins are separate. | Prove no HTTPS credential transfer or automatic HTTP login. |
| AUTH-13 | **Fixture-tested:** an authenticated primary-origin redirect reaches the observer without Authorization when followed with the required policy. | Network evidence from Chromium itself, not the fixture helper client. |
| AUTH-14 | **Fixture-ready:** independent path prefixes and protection spaces reject the wrong realm credential. | Chromium pre-auth/path reuse remains no broader than protection-space rules. |
| AUTH-15 | **Browser-only.** | Targeted cache clear, connection close, reload, and visible account chooser. |
| AUTH-16 | **Browser-only.** | Session logout without deleting saved entry. |
| AUTH-17 | **Browser-only.** | Delete saved account and see an empty next chooser. |
| AUTH-18 | **Browser-only.** | Never-save suppression and reset in settings. |
| AUTH-19 | **Fixture-tested:** plaintext route is HTTP and labels the expected warn/no-auto-login policy in response headers. | Prominent native warning and absence of browser auto-login. |
| AUTH-20 | **Fixture-tested:** Digest SHA-256/qop=auth challenge, success, failure, and nonce-count replay rejection; realms differ from Basic. | Chromium chooser/store integration and no Basic/Digest entry confusion. |
| AUTH-21 | **Browser-only.** | Explicit incognito selection and zero persistence. |
| AUTH-22 | **Browser-only.** | Last OTR-window close discards auth cache. |
| AUTH-23 | **Browser-only.** | Mac B, CloudKit, and iOS absence evidence. |
| AUTH-24 | **Browser-only.** | Touch ID/system-authenticated reveal. |
| AUTH-25 | **Fixture-tested (fixture scope):** structured log redacts Authorization and drops query strings; tests assert token/password/query absence. | Scrubbed AhoiBrowser logs, NetLog, crashes, and E2E artifacts. |
| AUTH-26 | **Browser-only:** no subresource page is claimed by this initial fixture. | Unambiguous visible subresource challenge with origin/frame context. |
| AUTH-27 | **Browser-only.** | Full installed, signed `/Applications/AhoiBrowser.app` Computer Use journey. |

## Release-gate boundary

The fixture closes no release gate on its own. In particular, the local
self-test's TLS context, redirect helper, in-memory nonce store, and synthetic
credentials are test mechanics. The release claim requires AhoiBrowser's real
`HttpAuthManager`/`LoginHandler`, password store, Keychain, OTR profile,
localization, accessibility, restart behavior, and network stack to pass the
matrix visibly in the installed signed bundle.
