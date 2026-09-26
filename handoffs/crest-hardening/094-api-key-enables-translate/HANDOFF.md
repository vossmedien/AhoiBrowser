# 094 – The Safe Browsing key also switches Google Translate on (N3)

Status: ready (owner decision, then a small change)
Owner lane: desktop
Base: HEAD `9f6cd15`.
Evidence: `artifacts/network-audit/build40-keyed-20260926-1115/` (H5, keyed
variant for PRIV-14).

## Finding

With a Google API key in the environment (`GOOGLE_API_KEY`, as for
`safe-browsing-journey.sh` and a later baked-in product key), build 40
fetches `GET https://translate.googleapis.com/translate_a/l?client=chrome&hl=de&key=…`
on the first navigation to a public page, without any translate UI. The
response is **200**: the endpoint ignores the key's API restriction to the
Safe Browsing API. Without a key the request never happened (builds 29–40,
keyless audits).

The cause is in Chromium: `TranslateManager` treats a missing key as "translate
unavailable" (`translate_manager.cc:201-209`, `kApiKeysMissing`; also `:745`
and `:919`). With a key, Translate becomes available. The language list then
refreshes itself through `TranslateLanguageList::GetSupportedLanguages`
(`translate_language_list.cc:223-226`) whenever translate is allowed. Accepting
a translation later sends the page's text to Google.

So the key doesn't only turn on Safe Browsing: it quietly turns on
Translate, which is denied rule N3 in `docs/NETWORK_SILENCE_CHECKLIST.md`.

## Decision (owner or user)

1. **Translate off by default** (recommended for network silence): register
   `translate.enabled` (`translate::prefs::kOfferTranslateEnabled`) with
   default `false` in Ahoi's profile prefs. The settings keep Chromium's toggle.
   Check that `translate_allowed` then stays false, so no `translate_a/l` is
   fetched until the user turns Translate on. N3 becomes "only after the user
   enables Translate", and the audit allowlist gets that condition.
2. **Translate on as in Chrome**: N3 becomes an allowed endpoint with a reason,
   and the privacy text says that translating sends page text to Google.
3. **Key only for Safe Browsing**: patch `TranslateManager` to treat Ahoi as
   keyless for Translate. Not recommended; it adds a patch for a feature
   decision that the pref already covers.

## After the decision

This lane reruns the keyed audit (`--google-api-key-from-keychain`) on the
next installed candidate. Expected with option 1: `translate.googleapis.com`
is absent, and fresh-profile silence and PRIV-12 pass again.
