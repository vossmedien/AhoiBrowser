# Workspace-local website sessions

Status: accepted user requirement, 2026-09-05; NOT implemented or accepted.
Desktop implements after closing its current browser-fix package. The frozen
startup candidate is not widened. This supersedes the former always-shared
workspace cookie/login rule, not the no-cookie-sync or native-engine boundaries.

## Product boundary

| State | Scope | Sync boundary |
| --- | --- | --- |
| Cookies, authenticated website sessions, HTTP-auth cache | Local website-session context assigned to the workspace | Never |
| Local Storage, IndexedDB, Cache Storage, service/shared workers and network context | Same native website-session context; Session Storage also retains upstream tab semantics | Never |
| History and password store | Global, using existing services | Existing permitted History policy only; passwords never |
| Extension installation, enablement and extension-owned storage | Global; not duplicated for each workspace | No raw extension storage; pins alone cannot install software or grant permissions |
| Transferable browser/extension settings and desired extension setup | Global by default; explicitly scoped workspace presentation overrides only | ADR 0010: supported native user preferences, trusted restore flow and positively reviewed extension-ID/key/value schemas |
| Extension action pins/order, workspace name/icon/accent | Workspace presentation | Suitable metadata; exact fields/category owned by Sync |
| Site permission decisions | Local session/origin context; native authority | Never; no inherited remote or other-context grant |
| Downloads | Global manager; any later preferred directory is device-local | No filesystem paths |

This separates website accounts, not users of the Mac. Global history/password
access and authorized global extensions remain visible by design. It is not
Incognito. Do not invent history silos, per-workspace password databases, a proxy
product or an extension installer as part of this request.

## Native integration constraints

- Keep Chromium's native Profile, BrowserContext, WebContents, Site Isolation,
  NetworkContext and storage ownership. No alternate cookie jar, header
  rewriting, manual token copying or persistent off-the-record emulation.
- Prefer the narrowest native isolation seam compatible with global services.
  The pinned M152 `content/public/browser/site_instance.h` exposes
  `CreateForFixedStoragePartition` (custom partition preserved across navigation).
  `storage_partition.h` owns cookies, DOM/IDB/cache/worker contexts. This is a
  concrete implementation lead, not proof that ordinary tabs, extension APIs,
  permissions, DevTools, restore and popups already support our use case.
- Select and persist the local context binding before the first navigation or
  restore request, including new tabs, opener popups, redirects, downloads and
  external links. A global active-workspace variable cannot authorize requests
  for background tabs or other windows.
- Same-context moves retain WebContents/state. Different-context moves require
  deliberate semantics without copying credentials or silently replacing the
  runtime target; never mutate a live WebContents' identity after navigation.
  Remote tree movement must remain lazy/preserving and cannot switch the local
  account of an already-open page. Keep global TreeNode identity separate.
- Preserve existing local cookies, databases and open tabs. Enabling a fresh
  isolated context must not silently migrate, copy, clear or log out an existing
  context. Shared/default-context compatibility needs an explicit local binding,
  not an accidental fallback when an isolated context cannot be resolved.
- Private tabs stay upstream off-the-record, wholly outside normal stores/sync.

The [upstream profile architecture](https://www.chromium.org/developers/design-documents/profile-architecture/)
explains why full Profile duplication also duplicates keyed services. The pinned
local headers, rather than an assumption about a current upstream API, must
determine the final small integration. Review that concrete routing before
writing engine hooks; no new parallel session/permission architecture.

### Concrete pinned routing findings — 2026-09-08

A bounded read-only helper identified the next routing seams; Desktop verified
the critical M152 implementations directly. The preferred implementation lead
is one regular Profile with persistent fixed StoragePartitions, not additional
full profiles or OTR emulation. `site_instance_impl.cc:244–255` explicitly creates
a non-Guest fixed partition. This is still planning, not implemented isolation.

| Initial seam (paths relative to Chromium src) | Required routing |
| --- | --- |
| `ahoi/browser/session/workspace_session_metadata.{h,cc}` | Persist the device-local website-session binding before requests, separate from mutable Workspace/Tree identity; existing default binding stays explicit. |
| `chrome/browser/ui/navigator/browser_navigator.cc`, CreateTargetContents | Select the correct SiteInstance before WebContents creation; preserve special Chrome/extension pages and opener relationships. |
| `chrome/browser/ui/browser_tabrestore.cc`, CreateRestoredTab | Restore the matching partition AND SessionStorageNamespaceMap, not only the URL/SiteInstance. |
| `chrome/browser/sessions/session_restore.cc:1095` | Recreate sessionStorage from that binding, not unconditionally DefaultStoragePartition. |
| `content/browser/web_contents/web_contents_impl.cc:5557` | Preserve a normal fixed partition for noopener while creating a separate BrowsingInstance; current code preserves only Guest partitions. |

This is not a complete five-entry feature plan: native permission and extension
cookie routing are required integration boundaries too. The live pinned
ContentSettingPermissionContextBase still reads a profile-wide SettingsMap;
PermissionContextBase explicitly notes that permissions are not partition-scoped.
The Chrome cookies API's ParseStoreCookieManager uses DefaultStoragePartition.
Consequently, simply selecting a partition for initial NewTab is insufficient.
Keep upstream permission/extension authority, global extension trust and native
special-page ownership; do not reinterpret pins as rights or swap global grants
when the foreground workspace changes. New isolated jars start empty; no
automatic copying, clearing or logout of existing website sessions.

## Sync coordination

Desktop owns native isolation and UI. Unified Sync owner
`01a06d69-1034-7372-b784-0b05a53c87e0` owns Common C++ and Swift/Wire. The renewed
request was sent in `01a07337-b020-7211-b95b-06878f84178f`; coordinator informed
in `01a07337-b056-7d73-95a1-9e3f0e6fce63`.

Use existing Workspace identity/appearance where possible. Review action-pin
metadata and, only if needed, a portable logical context assignment. Local
profile paths, native partition identifiers, permission grants and all website
state are not portable payloads. Unsupported extension IDs may be retained as
inert presentation metadata; a pin alone never installs or grants access.
The user has separately authorized full browser-setup restoration and suitable
extension settings under `decisions/0010-full-browser-setup-sync.md` (79d2102).
It is now required scope, not a hypothetical follow-up or inventory-only UI.
The Sync owner supplies the supported/excluded settings catalogue, native restore
adapter and positive extension-ID/key/value contracts. `chrome.storage.sync`
alone does not classify arbitrary data as safe. Native prompts/policies stay
authoritative; no raw storage, vault, credential, local-path or permission grant
transfer. iOS preserves recognized desktop setup without running Chrome add-ons.
This product-scope approval is not action-time approval of AnyChat's pending
website/New Tab rights or a new runtime/build lease.

Portable workspace identity/appearance/pins must stay distinct from each live
tab's device-local website-session binding. A remote workspace move cannot
relabel that binding or silently switch an already-open page's account. Prefer
the existing global Workspace/TreeNode IDs; do not invent a new wire context ID
or publish native partition/profile paths without an actual coordinated need.
No writer bump, new entity or modification of the current unified WIP is
authorized by a guessed field name. Freeze the smallest matching contract with
its owner; do not block the independent current startup correction on it.

## Acceptance

First a short visible journey on the exact signed candidate: two workspaces,
same controlled website, distinct synthetic accounts, workspace changes and
restart retain their own login; logout/clear in one leaves the other intact.
Then target the real popup/redirect/restore/context-transfer boundaries and
verify global History/password/extension behavior. Sync only allowed metadata
between matching clients; no secret/site-storage payload and no peer eager load
or account switch. Focused isolation checks follow that runnable journey, not
a new fixture/matrix project before working implementation.
