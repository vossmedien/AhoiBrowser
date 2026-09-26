# 060 – Developer toolkit acts on the tab's own StoragePartition

Status: ready
Owner lane: desktop (apply, build, test)
Base: HEAD `6867f1c` (`git apply --check` passes). By the crest-hardening lane;
syntax-checked read-only against `out/AhoiDev` (0 errors, including the
Chromium style plugin), not built.

## Finding (medium, same class as `4a11c52`)

Review of `4a11c52` (HTTP auth now resets the tab's partition: agreed; saved
credentials stay profile-wide, which matches ADR 0011's `website-sessions`
scope). A sweep for `GetDefaultStoragePartition` in the overlay found the
same defect in the developer toolkit:

- `ChromiumDeveloperCookieAdapter::GetCookieManager` reads, edits and
  deletes the **default** partition's cookies, whatever the active tab.
- `ChromiumBrowsingDataRemovalAdapter::Remove` clears "this site" cookies in
  the default partition, and its `BrowsingDataRemover` filter never names a
  partition, so cache, storage and service workers are also cleared only in
  the default partition.

In a Workspace with its own website sessions, the cookie editor shows the
shared Workspace's cookies, and "Clear site data" leaves the tab's own data in
place while deleting the shared Workspace's data for that site. The uBO uses
(`ubo_simple_url_loader_client.cc`) are browser-internal downloads and
correct as they are.

## Change

- `BrowsingDataRemovalAdapter::RemoveForTab(request, web_contents, cb)`, a
  virtual defined in the `.cc` whose default calls `Remove()` (fakes
  unchanged). `BrowsingDataController::ClearData` calls it with the
  validated tab.
- The Chromium adapter resolves the tab's partition
  (`GetPrimaryMainFrame()->GetStoragePartition()`) and uses it for the
  cookie clearing. For a non-default partition it also calls
  `filter->SetStoragePartitionConfig(...)` on the `BrowsingDataRemover`
  filter; only partition-scoped types are removed there, as the builder
  contract requires. The global "all sites" path is unchanged
  (profile/default).
- The cookie editor gets a
  `CreateChromiumDeveloperCookieAdapter(context, partition_config)`
  overload. The controller passes the active tab's partition config, and the
  adapter resolves it with `GetStoragePartition(config, can_create=false)`.

## Tests

- `DeveloperToolkitBrowsingDataTest.RemoveForTabDefaultsToRemove`.
- Visible check (WS-DEL/WS-ISO area): in a Workspace with its own website
  sessions, sign in to a test site that is also signed in in a shared
  Workspace. The cookie editor lists only the own Workspace's cookies.
  "Clear site data" signs out only that Workspace; the shared Workspace
  stays signed in.
