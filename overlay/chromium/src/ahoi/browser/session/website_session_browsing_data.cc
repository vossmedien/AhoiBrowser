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
#include "base/barrier_closure.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "components/browsing_data/content/browsing_data_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/storage_partition.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ahoi::session {
namespace {

using content::BrowsingDataFilterBuilder;
using content::BrowsingDataRemover;

// Flags that modify how data is removed but name no data of their own.
constexpr uint64_t kRemovalModifiers =
    BrowsingDataRemover::DATA_TYPE_AVOID_CLOSING_CONNECTIONS |
    BrowsingDataRemover::DATA_TYPE_LOGICAL_CLEAR;

struct Removal {
  base::Time delete_begin;
  base::Time delete_end;
  uint64_t remove_mask = 0;
  uint64_t origin_type_mask = 0;
  std::unique_ptr<BrowsingDataFilterBuilder> filter_builder;
};

// Receives, on the UI sequence, the configs of the own partitions that are
// loaded or on disk. `profile` is null only if `existing` is empty.
using ExistingPartitionsCallback = base::OnceCallback<void(
    Profile* profile,
    std::vector<content::StoragePartitionConfig> existing)>;

// Runs on a blocking-capable thread-pool sequence.
std::vector<bool> DirectoriesExist(std::vector<base::FilePath> paths) {
  std::vector<bool> exist;
  exist.reserve(paths.size());
  for (const base::FilePath& path : paths) {
    exist.push_back(!path.empty() && base::DirectoryExists(path));
  }
  return exist;
}

void FilterExistingPartitions(base::WeakPtr<Profile> weak_profile,
                              std::vector<WebsiteSessionBinding> bindings,
                              ExistingPartitionsCallback callback,
                              std::vector<bool> on_disk) {
  std::vector<content::StoragePartitionConfig> existing;
  Profile* profile = weak_profile.get();
  if (!profile || profile->ShutdownStarted()) {
    std::move(callback).Run(nullptr, std::move(existing));
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
    existing.push_back(config);
  }
  std::move(callback).Run(existing.empty() ? nullptr : profile,
                          std::move(existing));
}

// Finds the existing own partitions of `profile` without creating one; the
// check for their directories runs off the UI thread.
void WithExistingPartitions(Profile* profile,
                            ExistingPartitionsCallback callback) {
  std::vector<WebsiteSessionBinding> bindings =
      profile && !profile->IsOffTheRecord()
          ? WebsiteSessionBindingsForDataRemoval(profile->GetPrefs())
          : std::vector<WebsiteSessionBinding>();
  if (bindings.empty()) {
    std::move(callback).Run(nullptr, {});
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
      base::BindOnce(&FilterExistingPartitions, profile->GetWeakPtr(),
                     std::move(bindings), std::move(callback)));
}

void QueueRemovals(Removal removal,
                   WebsiteSessionRemovalsQueuedCallback queued,
                   Profile* profile,
                   std::vector<content::StoragePartitionConfig> existing) {
  for (const content::StoragePartitionConfig& config : existing) {
    std::unique_ptr<BrowsingDataFilterBuilder> filter =
        removal.filter_builder->Copy();
    filter->SetStoragePartitionConfig(config);
    // BrowsingDataRemover queues the task behind the running one; a nested
    // wait is not supported.
    profile->GetBrowsingDataRemover()->RemoveWithFilter(
        removal.delete_begin, removal.delete_end, removal.remove_mask,
        removal.origin_type_mask, std::move(filter));
  }
  std::move(queued).Run(std::move(existing));
}

void QueueOnExistingPartitions(Profile* profile,
                               Removal removal,
                               WebsiteSessionRemovalsQueuedCallback queued) {
  if ((removal.remove_mask & ~kRemovalModifiers) == 0 ||
      removal.filter_builder->MatchesNothing()) {
    std::move(queued).Run({});
    return;
  }
  WithExistingPartitions(
      profile,
      base::BindOnce(&QueueRemovals, std::move(removal), std::move(queued)));
}

// Keeps `model` alive past the removal call that may have run `done`
// synchronously, since that call still updates the model afterwards.
void ReleaseModel(std::unique_ptr<BrowsingDataModel> model,
                  base::OnceClosure done) {
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                             std::move(model));
  std::move(done).Run();
}

void RemoveOwnersFromModel(base::WeakPtr<Profile> weak_profile,
                           std::vector<BrowsingDataModel::DataOwner> owners,
                           base::OnceClosure done,
                           std::unique_ptr<BrowsingDataModel> model) {
  // The model reaches its partition through a raw pointer; a profile that
  // shuts down while the model loads takes the partition with it.
  if (!weak_profile || weak_profile->ShutdownStarted()) {
    std::move(done).Run();
    return;
  }
  BrowsingDataModel* model_pointer = model.get();
  base::RepeatingClosure removed = base::BarrierClosure(
      owners.size(),
      base::BindOnce(&ReleaseModel, std::move(model), std::move(done)));
  for (const BrowsingDataModel::DataOwner& owner : owners) {
    model_pointer->RemoveUnpartitionedBrowsingData(owner, removed);
  }
}

void RemoveOwnersFromPartitions(
    std::vector<BrowsingDataModel::DataOwner> owners,
    WebsiteSessionSiteDataRemovedCallback removed,
    Profile* profile,
    std::vector<content::StoragePartitionConfig> existing) {
  if (existing.empty()) {
    std::move(removed).Run({});
    return;
  }
  base::RepeatingClosure partition_done = base::BarrierClosure(
      existing.size(), base::BindOnce(std::move(removed), existing));
  for (const content::StoragePartitionConfig& config : existing) {
    // Loads a partition that exists only on disk, as the remover would.
    content::StoragePartition* partition =
        profile->GetStoragePartition(config);
    // No delegate: the data it adds (such as federated identity grants and
    // Topics) is profile wide and was handled with the default partition.
    BrowsingDataModel::BuildFromNonDefaultStoragePartition(
        partition, /*delegate=*/nullptr,
        base::BindOnce(&RemoveOwnersFromModel, profile->GetWeakPtr(), owners,
                       partition_done));
  }
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
                                  WebsiteSessionSiteDataRemovedCallback done) {
  // The data owner SiteSettingsHandler removes from the default partition:
  // the host for web origins, so a site-details page clears exactly its host
  // and a site group names each of its hosts and its eTLD+1.
  std::vector<BrowsingDataModel::DataOwner> owners;
  for (const url::Origin& origin : origins) {
    if (origin.opaque()) {
      continue;
    }
    if (origin.GetURL().SchemeIsHTTPOrHTTPS()) {
      owners.emplace_back(origin.host());
    } else {
      owners.emplace_back(origin);
    }
  }
  if (owners.empty()) {
    std::move(done).Run({});
    return;
  }
  WithExistingPartitions(profile,
                         base::BindOnce(&RemoveOwnersFromPartitions,
                                        std::move(owners), std::move(done)));
}

}  // namespace ahoi::session
