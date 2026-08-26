# AhoiBrowser Privacy Enforcement

`endpoint_allowlist_v1.json` is the versioned fresh-profile network contract.
Its default action is deny. A release feed is allowed only when the release
environment supplies a syntactically valid HTTPS `AHOI_SPARKLE_FEED_URL`; this
repository deliberately contains no invented production endpoint or secret.
Every allowed endpoint records its operator, purpose, payload class, retention
boundary, disable behaviour and failure behaviour. The network contract allows
component payload endpoints only over HTTPS and retains Chromium's signature
verification; a CDN plaintext redirect is a failing audit event, never an
allowlist exception.

The default profile enables HTTPS-Only mode and Safe Browsing standard
protection. Search suggestions, prediction and prefetch, ad Privacy Sandbox
APIs, Related Website Sets, usage metrics, crash-report consent and Variations
are disabled by default. Network-time queries and Chromium's AIM,
history-embedding and on-device/model-execution AI paths are also disabled by
the early product policy before FeatureList initialization. The Ahoi privacy
mode defaults to Chromium-compatible
website behaviour; stricter third-party-cookie enforcement is opt-in globally
or per origin. These are registry defaults, so explicit user values and managed
enterprise values keep normal Chromium preference precedence.

Strict mode blocks unpartitioned third-party cookies through Chromium's browser
and Network Service `CookieSettings`. It does not replace the normal decision
pipeline: partitioned CHIPS cookies remain usable; Storage Access grants,
extension schemes, explicit cookie exceptions and enterprise policy retain
their Chromium ordering. A compatibility exception is an exact HTTP(S)
top-frame origin. Off-the-record profiles do not inherit persisted exceptions.

Ahoi is deliberately not a content blocker. Neither privacy mode blocks ad,
analytics or third-party resource hosts such as scripts, images or frames.
Users who want broad request filtering install an extension such as uBlock
Origin; compatibility problems caused by an extension remain visible and
independently controllable through Chromium's extension system.

The URL loader throttle is limited to GPC, referrer reduction, removal of a
small documented set of navigation query parameters, and targeted reduction of
high-entropy User-Agent client hints on third-party subresource requests. It
keeps the low-entropy brand header and leaves first-party requests unchanged;
it does not spoof Web APIs or create a second content-filter engine. It is not
a cookie security boundary.

Run the static contract test with:

```sh
python3 -m unittest ahoi.browser.privacy.test.fresh_profile_network_audit_test
```

Run a dynamic audit against a built browser without disabling background
networking:

```sh
python3 ahoi/browser/privacy/test/fresh_profile_network_audit.py \
  --browser out/AhoiDev/AhoiBrowser.app/Contents/MacOS/AhoiBrowser \
  --output /tmp/ahoi-fresh-profile-network-audit.json
```

Evidence contains policy hashes, endpoint IDs and origins only. It never emits
URLs with queries, request headers, cookies or credentials. Any unknown
background origin fails the gate and must be investigated before the policy is
changed.
