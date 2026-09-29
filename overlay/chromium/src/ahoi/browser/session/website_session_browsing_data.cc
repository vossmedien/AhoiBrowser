// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/website_session_browsing_data.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "ahoi/browser/session/website_session_context.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/origin.h"

namespace ahoi::session {
namespace {

using content::BrowsingDataFilterBuilder;
using content::BrowsingDataRemover;

// Flags that modify how data is removed but name no data of their own.
constexpr uint64_t kRemovalModifiers =
    BrowsingDataRemover::DATA_TYPE_AVOID_CLOSING_CONNECTIONS |
    BrowsingDataRemover::DATA_TYPE_LOGICAL_CLEAR;

// What Settings' per-site "Delete data" removes from a partition.
constexpr uint64_t kSiteDataRemoveMask =
    BrowsingDataRemover::DATA_TYPE_COOKIES |
    BrowsingDataRemover::DATA_TYPE_DOM_STORAGE |
    BrowsingDataRemover::DATA_TYPE_DEVICE_BOUND_SESSIONS;
constexpr uint64_t kSiteDataOriginTypeMask =
    BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB |
    BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB;

struct Removal {
  base::Time delete_begin;
  base::Time delete_end;
  uint64_t remove_mask = 0;
  uint64_t origin_type_mask = 0;
  std::unique_ptr<BrowsingDataFilterBuilder> filter_builder;
};

// Runs on a blocking-capable thread-pool sequence.
std::vector<bool> DirectoriesExist(std::vector<base::FilePath> paths) {
  std::vector<bool> exist;
  exist.reserve(paths.size());
  for (const base::FilePath& path : paths) {
    exist.push_back(!path.empty() && base::DirectoryExists(path));
  }
  return exist;
}

void QueueRemovals(base::WeakPtr<Profile> weak_profile,
                   Removal removal,
                   std::vector<WebsiteSessionBinding> bindings,
                   WebsiteSessionRemovalsQueuedCallback queued,
                   std::vector<bool> on_disk) {
  std::vector<content::StoragePartitionConfig> reached;
  Profile* profile = weak_profile.get();
  if (!profile || profile->ShutdownStarted()) {
    std::move(queued).Run(std::move(reached));
    return;
  }
  // A Workspace deleted while the directories were checked is retired now;
  // its partition is removed by the Workspace deletion, not reloaded here.
  const std::vector<WebsiteSessionBinding> current =
      WebsiteSessionBindingsForDataRemoval(profile->GetPrefs());
  for (size_t i = 0; i < bindings.size(); ++i) {
    if (!std::ranges::contains(current, bindings[i])) {
      continue;
    }
    const content::StoragePartitionConfig config =
        StoragePartitionConfigForWebsiteSession(profile, bindings[i]);
    // Loaded but not yet written covers a Workspace created this run.
    if (!on_disk[i] &&
        !profile->GetStoragePartition(config, /*can_create=*/false)) {
      continue;
    }
    std::unique_ptr<BrowsingDataFilterBuilder> filter =
        removal.filter_builder->Copy();
    filter->SetStoragePartitionConfig(config);
    // BrowsingDataRemover queues the task behind the running one; a nested
    // wait is not supported.
    profile->GetBrowsingDataRemover()->RemoveWithFilter(
        removal.delete_begin, removal.delete_end, removal.remove_mask,
        removal.origin_type_mask, std::move(filter));
    reached.push_back(config);
  }
  std::move(queued).Run(std::move(reached));
}

void QueueOnExistingPartitions(Profile* profile,
                               Removal removal,
                               WebsiteSessionRemovalsQueuedCallback queued) {
  std::vector<WebsiteSessionBinding> bindings =
      profile && !profile->IsOffTheRecord()
          ? WebsiteSessionBindingsForDataRemoval(profile->GetPrefs())
          : std::vector<WebsiteSessionBinding>();
  if (bindings.empty() || (removal.remove_mask & ~kRemovalModifiers) == 0 ||
      removal.filter_builder->MatchesNothing()) {
    std::move(queued).Run({});
    return;
  }
  std::vector<base::FilePath> paths;
  for (const WebsiteSessionBinding& binding : bindings) {
    paths.push_back(WebsiteSessionPartitionPath(profile->GetPath(), binding));
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&DirectoriesExist, std::move(paths)),
      base::BindOnce(&QueueRemovals, profile->GetWeakPtr(), std::move(removal),
                     std::move(bindings), std::move(queued)));
}

}  // namespace

std::vector<WebsiteSessionBinding> WebsiteSessionBindingsForDataRemoval(
    const PrefService* prefs) {
  std::set<base::Uuid> retired;
  for (const PendingWebsiteSessionRemoval& pending :
       GetPendingWebsiteSessionRemovals(prefs)) {
    retired.insert(pending.context_id);
  }
  std::set<base::Uuid> seen;
  std::vector<WebsiteSessionBinding> bindings;
  auto add = [&](const std::optional<WebsiteSessionBinding>& binding) {
    if (binding.has_value() && !binding->is_default() &&
        !retired.contains(binding->context_id) &&
        seen.insert(binding->context_id).second) {
      bindings.push_back(*binding);
    }
  };
  for (const base::Uuid& workspace_id :
       GetWebsiteSessionBoundWorkspaceIds(prefs)) {
    add(FindWebsiteSessionBinding(prefs, workspace_id));
  }
  add(GetWebsiteSessionRecoveryBinding(prefs));
  return bindings;
}

void RemoveWebsiteSessionPartitionData(
    Profile* profile,
    base::Time delete_begin,
    base::Time delete_end,
    uint64_t remove_mask,
    uint64_t origin_type_mask,
    BrowsingDataFilterBuilder& filter_builder,
    WebsiteSessionRemovalsQueuedCallback queued) {
  // Only data types that live on a StoragePartition; profile-wide types were
  // handled by the removal that reached this.
  QueueOnExistingPartitions(
      profile,
      Removal{
          .delete_begin = delete_begin,
          .delete_end = delete_end,
          .remove_mask =
              remove_mask & BrowsingDataRemover::DATA_TYPE_ON_STORAGE_PARTITION,
          .origin_type_mask = origin_type_mask,
          .filter_builder = filter_builder.Copy(),
      },
      std::move(queued));
}

void RemoveWebsiteSessionSiteData(Profile* profile,
                                  const std::vector<url::Origin>& origins,
                                  WebsiteSessionRemovalsQueuedCallback queued) {
  std::unique_ptr<BrowsingDataFilterBuilder> filter =
      BrowsingDataFilterBuilder::Create(
          BrowsingDataFilterBuilder::Mode::kDelete);
  for (const url::Origin& origin : origins) {
    if (origin.opaque() || origin.host().empty()) {
      continue;
    }
    // Cookies are domain scoped, so the site is the unit that is removed.
    std::string site = net::registry_controlled_domains::GetDomainAndRegistry(
        origin, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
    filter->AddRegisterableDomain(site.empty() ? origin.host() : site);
  }
  QueueOnExistingPartitions(profile,
                            Removal{
                                .delete_begin = base::Time(),
                                .delete_end = base::Time::Max(),
                                .remove_mask = kSiteDataRemoveMask,
                                .origin_type_mask = kSiteDataOriginTypeMask,
                                .filter_builder = std::move(filter),
                            },
                            std::move(queued));
}

}  // namespace ahoi::session
