# Network behavior

Chromium's network service is authoritative. AhoiBrowser does not implement a
parallel HTTP stack. TLS verification, proxy support, DNS, HTTP cache,
authentication challenges, downloads, uploads, WebSockets, WebTransport, and
certificate UI follow Chromium unless a documented patch changes presentation.

## Fresh-profile endpoint budget

Every release runs a fresh-profile capture. Unprompted requests must match a
versioned allowlist with owner, purpose, payload class, retention expectation,
and disable/failure behavior. No endpoint may receive a stable Ahoi user ID.

Expected categories, subject to verified implementation, are:

- update metadata for the selected channel
- Standard Safe Browsing transport using the most privacy-preserving supported
  Chromium mode and a no-log/stateless proxy where technically valid
- user-triggered search/navigation suggestions when enabled
- CloudKit only after the user enables sync and the Apple account is available
- extension update checks only for installed extensions

Usage pings, experiment enrollment, promotional fetches, AI endpoints, and
automatic crash uploads are forbidden. Blocked network dependencies degrade
without delaying local startup or exposing browsing data.

## Developer overrides

Header modifications and CSP/CORS relaxation are explicit developer features.
Rules have stable IDs, scope, active duration, match previews, activity chips,
and a one-action emergency disable. Sensitive headers store only Keychain
references. Logs redact authorization, cookies, tokens, and configured secrets.
