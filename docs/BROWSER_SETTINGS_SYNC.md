# Native browser-setting sync

Current implementation packet, 2026-09-08. Common C++ and matching Swift are
owned by the unified Sync implementer; native Settings UI and Chromium builds
remain Desktop-owned. This is SOURCE, not build or two-Mac acceptance.
The larger outcome remains [ADR0010](decisions/0010-full-browser-setup-sync.md).

## Native settings catalogue

The positive catalogue is `sync/browser_setting_catalog.{h,cc}` below
`overlay/chromium/src/ahoi/browser/`. Each native profile additionally checks
actual registration, type and Chromium's Syncable/SyncablePriority flags. The
five existing Ahoi preferences keep their original identities and do not require
an upstream sync flag. The 23 real preference entries plus one native search
choice are an implemented subset, **not all of
chrome://settings** and not the completion boundary for ADR0010.

| Group | Exact preference IDs | Value contract |
| --- | --- | --- |
| Search choice | `ahoi.browser.default_search_engine` | Logical TemplateURLService choice: `duckDuckGo`, `google`, `bing`; no exported URL or shadow native preference |
| Ahoi | `ahoi.appearance.glass_enabled`, `ahoi.appearance.sidebar_page_tint_enabled`, `ahoi.navigation.floating_auto_hide_enabled`, `ahoi.navigation.floating_reveal_notch_enabled` | Boolean |
| Ahoi timing | `ahoi.navigation.floating_auto_hide_delay_ms` | Integer 100–10000 ms |
| Navigation | `browser.show_home_button`, `browser.show_forward_button`, `browser.pin_split_tab_button`, `browser.split_view_drag_and_drop_enabled` | Boolean |
| Downloads | `download.prompt_for_download`, `plugins.always_open_pdf_externally` | Boolean; no path or auto-open list |
| Language | `browser.enable_spellchecking`, `translate.enabled` | Boolean; no cloud-spelling consent |
| Encoding | `intl.charset_default` | Only `UTF-8` or `windows-1252` |
| Reading | `settings.a11y.read_anything.font_scale` | Finite double 0.5–4.5 |
| Reading appearance | `settings.a11y.read_anything.color_info` | Integer enum 0,1,2,3,4,5,7,8 |
| Reading spacing | `settings.a11y.read_anything.line_spacing`, `settings.a11y.read_anything.letter_spacing` | Integer enum 1,2,3 |
| Reading content | `settings.a11y.read_anything.links_enabled`, `settings.a11y.read_anything.images_enabled` | Boolean |
| Privacy/network | `enable_do_not_track`, `search.suggest_enabled` | Boolean |
| Preloading | `net.network_prediction_options` | Integer enum 0,1,2,3; 1 remains a current registered default |

All known entries also accept JSON `null` as an explicit reset, not as lack of
consent or a fresh installation. The native adapter converts numeric JSON to
the registered integer/double type and uses `ClearPref` for reset. It reads
**GetUserPrefValue**, never effective managed/extension/default values, and
preserves `IsUserModifiable` at application.

Excluded here: URLs and paths; arbitrary strings, dictionaries and site lists;
site grants, account/Google Sync/autofill/payment data; OS voice/font identifiers;
deprecated reading enum values. The coupled HTTPS-first preference pair is not
partially restored as independent toggles. Language lists, custom search engines,
other transferable settings and appropriate atomic groups need further native
mapping, not a claim that this first catalogue exhausts meaningful settings.

The Home target and `homepage_is_newtabpage` selector are BOTH excluded. A `false`
selector received on B would activate B's local homepage, not A's: an empty or
invalid target falls back to Chromium's new-tab page (ProfileImpl::GetHomePage),
while an existing target could name an unrelated local URL. The independent `browser.show_home_button` remains
supported, including Desktop's native Home toolbar control. A reviewed atomic
Home configuration is a future mapping; this correction does not authorize
arbitrary URL transfer. Existing local preferences/records are retained, not
deleted or rewritten. Build18 still has the earlier catalogue and is accepted
only for its unrelated search-choice journey until a new candidate includes
this source correction.

## One existing wire class and native authority

This packet uses the existing format3 `PermittedSettingRecord` and unchanged
three groups `setting_id`, `value_json`, `tombstone`; no writer/version bump,
SQLite migration, profile copy, provider, account or encryption system is added.
`BrowserSettingRecordId` preserves the previous native SHA256("setting:"+ID)
identity exactly (first16 bytes, entire hex nibbles12/16 overwritten by4/8).
Swift's matching catalogue uses those same IDs and value rules. Codec shape-only
fixtures with arbitrary record IDs still prove shape, not a native publishing ID.

The service observes registered USER values and stores a coalesced canonical
intent payload in the local-only `ahoi.sync.browser_setting_intents` dictionary.
Its original HLC is reused on retries/restart. Shared SQLite already containing
that version or a newer winner settles an old intent without reauthoring it.
Cleanup removes only the exact acknowledged payload, not a later local edit.
The old unstamped Upsert writer is removed. An opt-out does not tombstone a
global setting. Initial seeding waits for actual initial-fetch completion and
seeds only existing USER values, never absent defaults.

Native preference application is synchronous and suppresses only its own echo;
pending local intents take precedence until the backend settles them. Native
PrefService persists values through its normal store. Its CommitPendingWrite
closure is **not a boolean disk-success receipt**, and this packet never claims
that it is. A restart reapplies the shared winner rather than interpreting an
older native disk value as a new edit. Actual native disk-failure behavior still
belongs in the focused post-E2E safety check.

Per-setting consent leases are revoked synchronously on the UI thread, remain
revoked through off/on, and travel through backend SQL commit, pump selection,
Mac provider direct Upload, final CKRecord provider and delayed acknowledgment.
Blocked outbox rows stay stored; filtering precedes the accepted-row limit so
they do not starve other classes. CK cached pending IDs alone cannot supply a
missing approval. Refusing local upload at an account transition clears the
old setting approvals, retaining the actual native values and pending intents.
Original global/account/key authorization remains a separate prerequisite.

## Concrete native UI handoff and remaining acceptance

Public `ProfileSyncService` methods for the existing native Sync/settings page:

- `supported_setting_ids()` returns the catalogue subset registered on THIS
  profile; `permitted_setting_ids()` reports the selected subset.
- `SetBrowserSettingsSyncEnabled(bool)` changes the supported group in one
  deliberate category action, with no implicit startup/global-opt-in call.
- `SetPermittedSettingSyncEnabled(id,bool)` retains individual selection.
  `GetBrowserSettingCatalog()` supplies stable category keys for local UI labels.

Desktop should connect the compact browser-settings category to these methods,
then exercise its actual Chromium setting controls. Pinned M152's
PrefValueStore::NotifyPrefChanged (pref_value_store.cc:133–140) explicitly reports
changes from any backing store, including a USER-layer change whose effective
value is unchanged. JsonPrefStore::RemoveValue reports an actual removed USER
value; the adapter compares USER snapshots and captures its reset. No additional
upstream observer/action architecture is needed for that case. A truly absent
value without a native mutation remains absence, not an inferred fresh reset.
Unsupported/coupled settings and actual reset runtime proof remain open.

The native search adapter observes TemplateURLService and its existing user
selection record. It exports only unchanged code-owned built-in definitions,
does not replace custom/missing/ambiguous entries and uses the native fallback
reset path. It loads only when Sync is enabled; first load is observation, not
reset intent. Its posted self-apply callback compares the updated observation,
not a short-lived apply Boolean. No Google Sync processor is taken over.

Swift now binds that logical choice to the existing native search picker and
AppStorage. Its local domain commits before the native preference changes;
subsequent local projection/restart never creates a new remote authoring clock.
Record value clocks preserve actual local authorship and unchanged fields.
Only future explicit searches use the chosen engine; existing tabs do not load,
refocus or switch account. Reset means each platform's native default, not an
implicit forced provider. Other known desktop settings remain metadata on iOS.

A compact explicit Mobile browser-settings toggle controls the known IDs. Its
monotonic intent epoch reaches the provider synchronously before actor hops.
Ciphertext validation and original consent leases cover direct enqueue, seed,
domain-result staging, rehydration and the final delayed CKRecord callback,
including a recheck after system-field lookup. Unapproved records are retained,
not quarantined as invalid. Refusing local upload on account transition revokes
this category without deleting native preferences or domain records. Provider
and account cancellation/drain boundaries remain in force.

These are new SOURCE paths, still requiring a runnable candidate and real
acceptance. Extension desired install/enable state, reviewed extension settings,
broader appropriate settings and workspace action pins remain required packets.

Next acceptance: coherent product build, then a short visible configured MacA
to fresh linked MacB settings/default-reset journey, including already-open B
and local opt-out. Only afterward run the necessary focused intent replay,
authority/outbox and native-type checks. The single existing Ahoi setting test
is updated for USER/default/reset semantics but has not been executed here.
No Build17 Mobile or c20a759 Desktop result proves this new packet.
