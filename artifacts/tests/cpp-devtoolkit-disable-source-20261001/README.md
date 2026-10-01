# Toolkit disable source correction

The saved main-navigation, network metadata, direct injection and editor save
paths now use the same explicit/legacy activation predicate as the document
factory. Off mode preserves stored profiles and does not consume pending once
assets. A tab-owned native pref observer clears transient request/network state
and Ahoi's own UA on deactivation. An affected injected document is reloaded
through Chromium to retire arbitrary old JS/DOM work; enabling starts no replay.
A per-tab activation generation rejects main/document requests created before
an off/on transition, even if current bool/rules again match.

The editor preserves its controls/draft while closing its owned lazy compiler
service. Generation-bound replies cannot save old results after reactivation;
only the next explicit save starts a compiler. Save is refused before profile
construction/Keychain commit if the target is no longer enabled. A later native
surface's replacement UA is preserved rather than cleared by an old Ahoi marker.

Seven added regression methods cover dormant once assets, main-frame disable,
revocation before and after reply, native UA replacement, disabled editor save
and late compiler response across reenable. The editor methods inject a fake
compiler and null secret store; no real process or Keychain operation runs.
Existing positive runtime fixtures now explicitly activate the toolkit. The
18 document-Mojo methods also exercise a fourth deferred revocation variant:
off/on plus renewed metadata cannot approve the old factory generation.

Eleven exact pinned-Clang translation units and full ordered composition pass.
No link, executed regression, affected-page reload acceptance or overall DEV
completion is proved. Next candidate must run these regressions and visible
main/document/editor disable-reenable journeys, including unsaved draft and
unaffected native UA preservation. Shared source/output and installed app were
not changed by these checks. Build 60 remains signed d0688d88, installed 59.
Ready Crest 106/110 remain deferred to their future performance lease.
