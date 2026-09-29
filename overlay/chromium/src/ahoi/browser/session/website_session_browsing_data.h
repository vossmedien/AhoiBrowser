// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_WEBSITE_SESSION_BROWSING_DATA_H_
#define AHOI_BROWSER_SESSION_WEBSITE_SESSION_BROWSING_DATA_H_

#include <cstdint>
#include <vector>

#include "ahoi/browser/session/session_prefs.h"
#include "base/functional/callback_forward.h"
#include "base/time/time.h"
#include "content/public/browser/storage_partition_config.h"

class PrefService;
class Profile;

namespace content {
class BrowsingDataFilterBuilder;
}  // namespace content

namespace url {
class Origin;
}  // namespace url

namespace ahoi::session {

// Clearing browsing data reaches Workspace website sessions (crest 87d89354,
// adoption review A2). Chromium's remover clears only the default
// StoragePartition unless a filter names another one, so a Workspace with its
// own website sessions would keep its logins after "Delete browsing data".

// Own website-session contexts a data removal must reach: each Workspace's
// own context (once, even if copies share it) and the recovery context.
// Shared Workspaces live in the default partition; a context pending removal
// is retired and is never reached again.
std::vector<WebsiteSessionBinding> WebsiteSessionBindingsForDataRemoval(
    const PrefService* prefs);

// Receives the partitions that got a queued removal, possibly none.
using WebsiteSessionRemovalsQueuedCallback =
    base::OnceCallback<void(std::vector<content::StoragePartitionConfig>)>;

// Queues a removal of `remove_mask`'s StoragePartition data types, scoped by
// a copy of `filter_builder`, on every existing own website-session
// partition of `profile`. A partition that is neither loaded nor present on
// disk is skipped, so a removal never creates one. The check for the
// directory runs off the UI thread; `queued` runs on the calling sequence
// once every removal is queued with the profile's BrowsingDataRemover. The
// removals themselves complete later, one after another, like the Isolated
// Web App removals Chromium queues from the same place.
void RemoveWebsiteSessionPartitionData(
    Profile* profile,
    base::Time delete_begin,
    base::Time delete_end,
    uint64_t remove_mask,
    uint64_t origin_type_mask,
    content::BrowsingDataFilterBuilder& filter_builder,
    WebsiteSessionRemovalsQueuedCallback queued);

// Settings' per-site "Delete data" goes through a BrowsingDataModel of the
// default partition only. This removes the cookies and site storage of the
// sites of `origins` (registrable domain, or host for IP addresses and
// internal names) from every existing own website-session partition.
void RemoveWebsiteSessionSiteData(Profile* profile,
                                  const std::vector<url::Origin>& origins,
                                  WebsiteSessionRemovalsQueuedCallback queued);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_WEBSITE_SESSION_BROWSING_DATA_H_
