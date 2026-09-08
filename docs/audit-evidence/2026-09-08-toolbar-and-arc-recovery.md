# Toolbar follow-up and real Arc recovery — 2026-09-08

## Exact candidate

The existing detached build snapshot at715afc21a9757ea46a309817691c5c4aaa320c1c
contains the completed3d59 package plus only the user's56px address-bar activator,
LEFT pin placement, removal of the duplicate horizontal bookmark surface, and
the verified codesign certificate-prefix tooling fix. Common extension WIP and
patch0036 are not included. The approved df03d46 branding asset/generator paths
are byte-identical; no separate icon build.

Guarded overlay10765 EXIT0; app-only build98973 EXIT0. jobs2, fresh70.7%CPU idle,
52%memory headroom, no new swapouts, documented49.9GiB build override above32GiB
hard floor. The99-task incremental frontier restatted after78 tasks. Original
build/signature verification succeeded; no programmatic test suite was run.

Canonical evidence directory:
`artifacts/build/desktop-toolbar-left-715afc2-20260908/`.

- Original build receipt SHA:
  `73cb67db12cfb3d64644fddc63f7be3b40d0b64b6a7d799676255fd6ba563d3a`.
- Original binary:
  `4d98e259a439be7eb5319317020ffacebc4f5eda07ce4fa0598035e39b82b69e`.
- Original bundle tree:
  `74d9aa8f67889f072c2b1621e1866a747c83071fd4a63fbd7e92933d97f22936`.

An exact separate copy undercloudkit/AhoiBrowser.app was prepared41500 EXIT0,
Apple-Development signed EXIT0 and verified28337 EXIT0, using real OSX profile
8f149b92-89cc-4d34-a0db-1b305d4e545c and exact Development runtime/claims.

- cloudkit/verification.json SHA:
  `bc367cef3da0d0b08950ea6cf4ce345685b6289e31950e80f198611bcdd09031`.
- Signed copy binary:
  `667626cd56c98d0768966b6cecf2be7b0fae2e69d6496164c92acdcca267eb45`.
- Signed copy tree:
  `99bc79f5e44489d7d4e609849c1cc3f4ebf0a28be463fb8e2e8ea3723fbae95d`.

The original3d59 provider-free and Development copies plus their receipts remain
preserved, not relabelled. None of these signatures is Production/transport proof.

## Actual installer collision, corrected without deletion

Initial715 installation26297 EXIT2 before staging or activation: the deterministic
rollback path for currently installed4cb already held its preserved older copy.
Source4cb and binary55301ccbda32e32d3ee57420bd10adc3581b96a918047dbbb82815a56134770b
were verified, no process used that exact executable, and the destination was
absent. Only that owned older backup was renamed on the same filesystem:

`/Applications/.AhoiBrowser.rollback-v0.0.1-b1-s4cb622a0bffc-hd795fa7d5f7f.app`
to`/Applications/.AhoiBrowser.retained-rollback-4cb622a-d795fa7d-20260908.app`.

Main binary hash remained identical. Nothing was deleted/overwritten; historical
receipts naming the older path should follow this retained-path mapping. The
original install.log failure remains. Corrected guarded retry92150 is EXIT0,
logged in install-retry.log. Installed source/binary match the signed copy.
Install receipt `artifacts/install/ahoi-dev-715afc2-cloudkit-20260908.json` SHA:
8969a2f1f884f0d08c806943691b170b62e33905c0bdc99feb26c2882062b767.

## Bounded visible715 acceptance

Explicit normal executable launch with
`--user-data-dir=/private/tmp/ahoi-toolbar-715afc2.enwD8P`, no-first-run and
no-default-browser-check; initial public URL
https://example.com/?ahoi-toolbar-715afc2. Installed binary was hashed before
launch. CUA selected the full installed app path only after that CLI start.

Observed sequence, not a generated fixture:

1. Real Example page; broadened activator opens native toolbar. Pin is the first
   left-hand control; checking it changes name to Adressleiste lösen/value1.
2. Native Home opens chrome://newtab/ in the active tab. Toolbar remains pinned.
3. Native macOS Bookmarks menu -> bookmark this tab -> Bookmark-bar folder ->
   Done creates one Example Domain bookmark visible in the sidebar. Only one
   bookmark toolbar exists in AX; normal page AND NTP show no horizontal bar.
   Later clicking that real bookmark opens its Example URL.
4. Ahoi settings shows category0/24 and global Sync OFF. Explicit category click
   yields24/24 plus the truthful paused/until-global-enable explanation; global
   Sync stays OFF.
5. Real Cmd+Q (`super+q`) ends runtime85457 EXIT0. Explicit same-profile restart
   preserves checked Pin, bookmark and24/24 category; global Sync remains OFF.
   Category was then disabled and the normal address-bar route returned to
   Settings to visibly confirm0/24 (not inferred merely from clicking).
6. Pin off plus page click hides the complete toolbar. The broad activator
   reopens it; Pin was re-enabled for the visible handoff.
7. About/Help visibly renders the new sail logo matching the approved source.
   This is the theme-logo surface, not a claim about every Dock cache/render size.

All CUA captures and AX observations are in this conversation. No formal release
PASS matrix is manufactured. Restart runtime50070 remains active in the isolated
profile; its log is artifacts/e2e/desktop-toolbar-715afc2-20260908/restart.log.
The real Default DB/journal hashes below remain unchanged after the715 journey.

Limits: no pointer-hover API exists in current CUA; a right-click/focus Reload
observation showed no former hard rectangle, but does not establish every hover/
pressed/focus variant. Pinned transparent navigation lets a sticky Settings title
remain visible underneath; no speculative second layout fix was included here.
One Back action around NTP did not establish the intended Settings return; the
ordinary typed chrome://settings/ahoi route succeeded, so no full navigation-
history pass is claimed. Component ANGLE duplicate-class warnings and shutdown
rendezvous diagnostics remain visible, not attributed to a product failure.

The UI says CloudKit is unavailable in this build while global Sync is OFF.
Exact715 source refuses provider initialization when transport_enabled_ is false;
provider_available=false therefore does NOT diagnose missing configuration or a
failed key read. The physically configured/signed native candidate is verified,
but no global enable, key bootstrap, zone write or roundtrip was attempted.

## Real Arc recovery: refused for an unrelated local navigation

The compatible4cb was deliberately activated through guarded install79760 EXIT0
before native normal-temp mirroring could affect the real failed-import profile.
After earlier external-window changes, fresh stable UI allowed the real path:
Sidebar Settings -> Import bookmarks/settings -> Arc -> Search -> Restore.
The handler completed with recoveryRequired; no restore/import passed.

Read-only native4cb fingerprint reconstruction was checked against its actual
backup, including exact signed64-bit timestamps, UTF-16 strings and undo order:

- Backup:170685df5a8c2a96fe6cedc529bcff08d716fc8678861d6ac11865c3b068e06f,
  matching journal.previous_tree_sha256 exactly;1 workspace/5 nodes/5 undo.
- Current:d8409d52a4c3e1cc954eb5539318bb723810ad2e6000e3c7812fd916ad214493;
  2 workspaces/174 nodes/5 undo.
- Journal expected:e1b3928cf8c507e008f17a3485405b8a89b8e4bcd3f839398b50bf521722a8fa.

The only changed baseline row is non-imported Page
9b4a0498-3b35-4b51-a48d-8239b51d001a (title/url/modified_at). Existing workspace
and undo rows are identical. Substituting ONLY that baseline page's backup fields
in a comparison model yields the exact journal.expected fingerprint. The
imported delta is unchanged; blindly restoring the complete backup would lose
the unrelated navigation. No URL values/secrets were emitted by the diagnostic.

The journal remains prepared/manual_recovery_required,170 affected IDs,7 planned
native IDs and no native receipt. Its raw SHA stays
1768e20f145a7e949281eea58f33ef14a2a80e34a1430b1caf3d704a9cddd5a3. Backup is intact.
The dialog was cancelled, then4cb quit through its native app menu. A tool-only
CMD key spelling error performed no keyboard action and is not a Cmd+Q product
failure. After normal persistence the raw tree DB SHA is
791e9ae10ed951cb8503ee0a29d863d768bb35969e16553549d4175f64795ff0.

The bounded preserving recovery-plan implementation is delegated in Desktop's
Arc-only source scope. It must retain unrelated local edits and all original
journal/live/durable-tab checks; no forced rollback or profile/schema mutation.
Current715 visible acceptance will use an explicitly isolated user-data directory,
not auto-launch Default. Toolbar/icon/settings acceptance and the corrected real
Arc recovery remain separate pending runtime gates.

## e241 preserving candidate: second, concrete native load failure

Arc-only e24119158e2d15c8bc9f0b22d0fea5e555327e5a built56659 EXIT0 and was
Development-prepared73966/signature-verified22013/installed74357 EXIT0. Before
starting the real Default profile, its existing global Sync preference was
read as true. The identical compiled provider-free candidate was therefore
installed through34545 EXIT0, preserving the prepared Development copy and
without toggling that preference or accessing keys. Provider-free install
receipt SHA:dd876c0796d026fb6b450fa8534d4058e66256dbed27e5a94c152fc9a4dfe854.

Runtime94861 used the explicit real user-data directory/Default. The normal
import UI still rejected recovery. Unlike the4cb navigation-hash refusal,
the actual new failure was earlier: native SQLite's ADD COLUMN with CHECK
attempted an unavailable internal pragma_quick_check table. The real disk tree
never loaded, so the visible Inbox was only an in-memory bootstrap, NOT a new
persisted workspace or a reason to weaken the preserving plan. Exact failure:

    no such table: pragma_quick_check
    ALTER TABLE tree_nodes ADD COLUMN is_temporary INTEGER NOT NULL DEFAULT 0 CHECK(is_temporary IN (0,1))

Log:artifacts/e2e/desktop-arc-preserve-e241191-20260908/runtime.log. Native source
confirms the pinned SQLite alter.c ADD-CHECK validation path. Real tree remained
Schema2,2 workspaces/174 nodes and raw SHA791e9ae...; journal1768e20f... unchanged
after normal Cancel/Cmd+Q and runtime EXIT0. No recovery/import pass and no
backup overwritten. The later read-only and live observations supersede the
earlier unverified suspicion that a persistent Inbox caused this refusal.

The minimal correctionad91502 uses the existing outer SQL transaction and
CreateSchema constraints: rename node tables, recreate/copy every field, restore
parents after every new row exists, drop old tables, recreate indexes before
commit. FK/CHECK/Unique constraints stay enabled; workspaces/undo sequence/meta
are retained. No Raze, PRAGMA-off, ignored failure or empty fallback. One existing
Schema2 regression is added, not run before the corrected visible journey.

The user's further UI requests are bundled with that real correction:
fe647ee adds quiet saved-section tint, visual-empty validated drop highlight and
an indexed BookmarkModel-derived favicon star;9323d71 shortens the recovery notice
and action with16px separation. Main reviewed the disjoint helper implementations.
The clean combined6b6c771 candidate is now building; see the active checkpoint.
