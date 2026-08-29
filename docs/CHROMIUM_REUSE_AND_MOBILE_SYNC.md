# Chromium reuse and Ahoi mobile sync

## Product decision

AhoiBrowser reuses Chromium browser primitives wherever their ownership and
data model fit. It does not run a second copy of Chrome Sync and does not make a
Google browser account a prerequisite. The shared Ahoi domain remains
local-first; CloudKit is the Apple-platform transport and can be replaced by a
platform-appropriate provider without changing browser UI semantics.

The intended presentation is deliberately closer to Arc than to Chrome's
separate history page:

- a page saved on iPhone or iPad appears permanently in the same Ahoi
  workspace/folder tree on desktop;
- an open, unsaved mobile tab appears temporarily in the desktop sidebar under
  "Tabs from other devices";
- each temporary row carries an iPhone, iPad, desktop, or generic-device icon;
- accessible name and tooltip include device name, workspace, relative activity
  time, and online/offline state;
- opening a remote row creates a normal local tab; "take over" also saves that
  tab at the current workspace root;
- private tabs are neither published nor accepted.

## Chromium capability audit (M152 checkout)

| Capability | Chromium already provides | Ahoi decision |
| --- | --- | --- |
| local tab lifecycle and observation | `TabStripModel`, `TabInterface`, `WebContents` | reuse directly |
| local session restore | `SessionRestore` and browser session services | reuse directly |
| tab search and recent tabs | standard browser UI/services | reuse presentation and lifecycle primitives |
| favicons and navigation | favicon services, `NavigateParams`, safe URL types | reuse directly |
| tabs from other devices | `SessionSyncService`, `OpenTabsUIDelegate`, foreign-session side panel and recent-tabs menu | reuse labels, interaction patterns, accessibility, and suitable view primitives; do not use its Chrome-Sync data source |
| send tab to self | `send_tab_to_self` bridge and UI | reuse product semantics where useful; implement through signed Ahoi remote commands rather than Chrome Sync |
| saved tab groups | `TabGroupSyncService` and saved-tab-group model | reuse local tab/group behavior selectively; Ahoi workspaces have a deeper folder/tree contract and require an adapter, not a wire-format substitution |
| Chrome Sync transport | session, open-tabs, saved-group, and send-tab model types | do not enable; Ahoi uses its own encrypted provider boundary |

Chromium's standard cross-device surfaces are not transport-neutral. Their
production data arrives through Chromium sync model types and account state.
Pointing the existing UI at Ahoi data therefore requires an adapter or an Ahoi
surface; merely enabling the feature would either remain empty or reintroduce
Chrome Sync.

## Existing Ahoi desktop extension

The Ahoi overlay already contains the correct transport-neutral extension:

- `sync::ProfileSyncService` owns profile-scoped publication and download;
- `sync::DeviceTabsService` filters the local store into local/remote snapshots,
  excludes unsafe/incognito/tombstoned data, validates active sessions, and
  notifies observers;
- `BrowserSidebarHostView` publishes normal Chromium tabs and observes the
  device snapshot;
- `sidebar_remote_tab_views` renders favicon, per-device icon, tooltip,
  accessibility name, and context actions inline in the main Ahoi sidebar;
- workspace/tree synchronization uses the same `TabTreeStore` consumed by the
  desktop sidebar, so a saved mobile page is a real workspace node rather than
  a duplicate "mobile bookmark".

This avoids a second user-facing tab manager and preserves Ahoi's sidebar UX.
An optional future adapter may also expose the Ahoi snapshot through Chromium's
recent-tabs entry points, but it must remain a view adapter only. The Ahoi store
and provider stay authoritative.

## Mobile publication lifecycle

The native mobile browser source publishes only normal HTTP(S) tabs. The
publication uses stable tab and session UUIDs and the existing
desktop-compatible wire codec. The app reconciles the complete restored browser
session at launch and on relevant lifecycle/browser events:

1. restored normal tabs missing from the device projection are republished;
2. records no longer present in the browser session become tombstones;
3. device and session liveness are refreshed;
4. URL, title, workspace, and saved/pinned changes update the same stable tab;
5. closing through either the tab switcher or page menu closes the published
   tab;
6. saving to a workspace republishes the tab with its workspace and saved state;
7. CKSyncEngine automatic transport and delegate callbacks import remote
   changes; launch, foreground activation and the manual action are explicit
   reconciliation triggers, with no foreground polling loop;
8. private tabs remain outside the repository, outbox, CloudKit, and desktop UI.

Local mutations commit before network work. A missing account, key, container,
entitlement, or network leaves local browser state usable and the outbox
pending.

## Evidence boundary

Source integration and deterministic wire compatibility do not prove seamless
sync. Release acceptance requires one signed
macOS AhoiBrowser and one signed physical iPhone/iPad using the same production
configuration, then visible proof of all of the following:

- mobile open, navigate, rename, save, move, close, and reopen propagate to the
  correct desktop temporary row or persistent workspace node;
- desktop mutations propagate back without duplicates;
- device icon/name, workspace, relative time, and liveness are correct;
- foreground/background, offline queue, reconnect, concurrent edits,
  tombstones, account switch, zone recovery, and revoked device behavior are
  understandable and converge;
- private tabs, cookies, credentials, permissions, site data, and extension
  storage never appear in the provider or remote UI;
- no Google login and no Chrome Sync activation are required.

Until the signed two-device run exists, the correct status is
`BLOCKED_ENTITLEMENT`/`NOT_RUN`, not "seamless sync passed".

## Concurrent desktop work boundary

The desktop sync/sidebar overlay is currently being changed in the canonical
checkout by another agent. This mobile branch treats those files as read-only.
After both streams finish, integration must preserve both histories, reconcile
the current desktop APIs with the mobile wire contract, repeat visibly affected
E2E journeys first, then run programmatic gates and merge the integrated result
into the canonical default branch.
