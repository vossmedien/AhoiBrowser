# Fresh-profile network silence checklist (Chromium 153)

Origin: Crest-Konvergenz-Härtung H5.2. The checklist compares
ungoogled-chromium `153.0.8010.52-1` (commit `a638756`; macOS
repository `038db2b` is still on 152) with Ahoi at `f811604`. It is a work
list for the fresh-profile network audit (`docs/NETWORK.md`, `PRIV-*`), not
evidence. Rows marked *unverified* still need observation on a candidate.

Ahoi deliberately **keeps** Safe Browsing (Standard), the component updater
(Widevine, security components) with enforced secure transport, Sparkle and
extension updates for installed extensions. It therefore does not adopt
ungoogled's domain substitution or blanket request blocking.

## Already silenced in Ahoi

- Profile prefs (`O/privacy/privacy_defaults.cc`): search suggestions and
  network prediction off, Google sign-in disallowed, Privacy Sandbox off.
  Safe Browsing Standard stays on.
- Local State prefs: network time queries off, metrics reporting off,
  variations restricted.
- No Google API key or OAuth client. This keeps autofill-server,
  optimization-guide hints, variations seed and Gaia account flows quiet.
- Audit allowlist `O/privacy/config/endpoint_allowlist_v1.json` is
  default-deny; allowed hosts are the update, gvt1 and Safe Browsing hosts.

## Work list

| # | Category | Ahoi status | Action | Endpoints in the audit |
| --- | --- | --- | --- | --- |
| N1 | GCM check-in / FCM registration | **Active on every fresh profile.** Unmanaged profiles start policy and remote-command invalidation listeners through `UserFmRegistrationTokenUploader` | Handoff [004](../handoffs/crest-hardening/004-gcm-policy-invalidations/HANDOFF.md) | `android.clients.google.com/checkin`, `/c2dm/register3`, `mtalk.google.com:5228` (plain TCP) |
| N2 | Field-trial testing config | **Active.** Unbranded builds apply `testing/variations/fieldtrial_testing_config.json`, including official builds | Handoff [005](../handoffs/crest-hardening/005-fieldtrial-testing-config/HANDOFF.md): `disable_fieldtrial_testing_config = true` | changes feature state; rerun the full audit |
| N3 | Translate language list | Not handled; fetched the first time translate is offered | Decide: pref `translate.enabled=false` by default, or allow it as user-driven | `translate.googleapis.com/translate_a/l`, `/element.js` |
| N4 | On-device model / optimization-guide downloads | *unverified*; a fresh profile held SODA, SODA language packs, TranslateKit, OnDeviceHeadSuggestModel, WasmTtsEngine and DictationConnector component folders | Inspect `chrome://components` on the candidate; each component needs a reason or a disable | `update.googleapis.com` (component), `optimizationguide-pa.googleapis.com` |
| N5 | NTP promos / One Google Bar | *unverified*; depends on whether Ahoi's new-tab page uses Chrome's NTP WebUI | Audit the new-tab path | `www.google.com/async/newtab_*`, `ogs.google.com` |
| N6 | Intranet redirect detector | Not handled | Optional: policy or feature switch | DNS lookups of random 7–15-character hostnames |
| N7 | DIAL / mDNS discovery | Starts only when the Cast dialog is used | Optional | `239.255.255.250:1900`, `224.0.0.251:5353` |
| N8 | Reader mode fonts | Reader (DOM distiller) may load Google Fonts | Watch during reader tests | `fonts.googleapis.com` |
| N9 | Safe Browsing extras | Standard kept | Check that extended reporting, client-side-detection pings and HaTS stay off | `safebrowsing.google.com/safebrowsing/clientreport` |
| N10 | Crash upload | Consent-gated, crashes stay local | Confirm no upload URL in the release build | `clients2.google.com/cr/report` |
| N11 | Extension updates | Only with installed extensions | Add to the allowlist as conditional | `clients2.google.com/service/update2/crx` |

Quiet by default, keep watching:
- variations seed fetch
- UMA/UKM and domain reliability
- network time
- autofill server
- Gaia `ListAccounts`
- WebRTC log upload
- RLZ
- Hunspell dictionaries (macOS uses the system spellchecker)

## Audit procedure additions

1. Capture DNS and plain TCP, not only HTTP(S). MCS (`mtalk.google.com:5228`)
   is not HTTP.
2. Fresh profile, 10 minutes idle, then one ordinary page, then quit. After
   quitting, check that the profile's GCM store has no `gservice1-android_id`
   (proves N1 on disk).
3. Record every component under the profile and user-data directory with its
   first-download trigger (N4).
4. Separately and user-driven: one VAPID Web Push subscription on a test
   page, to learn whether Web Push works in Ahoi at all. `DEPRECATED_ENDPOINT`
   answers suggest that Google rejects `register3` for unbranded, keyless
   builds (see handoff 004).
