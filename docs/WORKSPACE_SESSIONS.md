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
A separately authorized extension-restoration/settings feature remains with the
Sync owner and must use its own reviewed source/consent/secret-filter contract.
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
