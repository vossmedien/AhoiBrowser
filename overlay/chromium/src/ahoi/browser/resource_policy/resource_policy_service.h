// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_SERVICE_H_
#define AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_SERVICE_H_

#include <map>
#include <memory>
#include <set>

#include "ahoi/browser/resource_policy/resource_policy_types.h"
#include "base/callback_list.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "chrome/browser/performance_manager/public/user_tuning/user_performance_tuning_manager.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/download/public/common/download_item.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/download_manager.h"

class BrowserWindowInterface;
class PrefService;
class Profile;
class TabStripModel;

namespace tabs {
class TabInterface;
}

namespace ahoi::resource_policy {

// Profile-scoped projection and safety layer over Chromium's lifecycle stack.
// Automatic timing and candidate selection stay owned by MemorySaverModePolicy
// and PageDiscardingHelper. This service observes product-critical states,
// projects sleeping/waking UI state and exposes the explicit user action.
class ResourcePolicyService final : public KeyedService,
                                    public ProfileObserver,
                                    public BrowserCollectionObserver,
                                    public TabStripModelObserver,
                                    public content::DownloadManager::Observer,
                                    public download::DownloadItem::Observer,
                                    public performance_manager::user_tuning::
                                        UserPerformanceTuningManager::Observer {
 public:
  using StatusChangedCallback =
      base::RepeatingCallback<void(tabs::TabInterface*,
                                   const TabResourceStatus&)>;

  explicit ResourcePolicyService(Profile* profile);
  ResourcePolicyService(const ResourcePolicyService&) = delete;
  ResourcePolicyService& operator=(const ResourcePolicyService&) = delete;
  ~ResourcePolicyService() override;

  // KeyedService:
  void Shutdown() override;

  // ProfileObserver:
  void OnProfileInitializationComplete(Profile* profile) override;

  TabResourceStatus GetTabStatus(tabs::TabInterface* tab) const;
  bool IsTabSleeping(tabs::TabInterface* tab) const;
  bool CanSleepTab(tabs::TabInterface* tab) const;
  bool SleepTab(tabs::TabInterface* tab);
  bool WakeTab(tabs::TabInterface* tab);

  bool IsNeverSleep(tabs::TabInterface* tab) const;
  bool SetNeverSleep(tabs::TabInterface* tab, bool enabled);
  bool IsMemorySaverEnabled() const;
  bool IsMemorySaverManaged() const;
  bool SetMemorySaverEnabled(bool enabled);

  // Holds Chromium's auto-discardable bit false for the exact critical-flow
  // lifetime. Destroying the returned token releases only this Ahoi hold and
  // restores the prior upstream state; there is no polling or Ahoi timer.
  [[nodiscard]] base::ScopedClosureRunner AcquireCriticalFlowProtection(
      tabs::TabInterface* tab,
      CriticalFlow flow);

  base::CallbackListSubscription AddStatusChangedCallback(
      StatusChangedCallback callback);
  ResourcePolicyEvidence CollectPerformanceEvidence() const;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // DownloadManager::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void ManagerGoingDown(content::DownloadManager* manager) override;

  // DownloadItem::Observer:
  void OnDownloadUpdated(download::DownloadItem* item) override;
  void OnDownloadRemoved(download::DownloadItem* item) override;
  void OnDownloadDestroyed(download::DownloadItem* item) override;

  // UserPerformanceTuningManager::Observer:
  void OnMemorySaverModeChanged() override;

 private:
  class TabSignalsObserver;
  struct TrackedTab;

  void InitializeRuntime();
  bool ShouldTrackBrowser(const BrowserWindowInterface* browser) const;
  void TrackBrowser(BrowserWindowInterface* browser);
  void UntrackBrowser(BrowserWindowInterface* browser,
                      bool tab_strip_model_destroyed);
  void TrackTab(TabStripModel* model, tabs::TabInterface* tab);
  void UntrackTab(tabs::TabInterface* tab);
  void UpdateTrackedContents(tabs::TabInterface* tab);
  void RefreshTab(tabs::TabInterface* tab);
  void RefreshAllTabs();
  void SetWaking(tabs::TabInterface* tab, bool waking);

  CriticalSignals CollectCriticalSignals(tabs::TabInterface* tab) const;
  SleepBlockReason GetUpstreamBlockReason(tabs::TabInterface* tab,
                                          bool ignore_recent_visibility,
                                          bool* eligible) const;
  bool HasInProgressDownload(tabs::TabInterface* tab) const;
  void ApplyAutomaticProtection(tabs::TabInterface* tab,
                                const CriticalSignals& signals);
  void AddCriticalFlowProtection(tabs::TabInterface* tab, CriticalFlow flow);
  void ReleaseCriticalFlowProtection(base::WeakPtr<tabs::TabInterface> tab,
                                     CriticalFlow flow);

  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<content::DownloadManager> download_manager_ = nullptr;
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  base::ScopedObservation<ProfileBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  std::map<BrowserWindowInterface*, raw_ptr<TabStripModel>> windows_;
  std::map<TabStripModel*, raw_ptr<BrowserWindowInterface>> models_;
  std::map<tabs::TabInterface*, std::unique_ptr<TrackedTab>> tabs_;
  std::map<download::DownloadItem*, bool> download_in_progress_;
  std::set<raw_ptr<download::DownloadItem>> observed_downloads_;
  base::RepeatingCallbackList<void(tabs::TabInterface*,
                                   const TabResourceStatus&)>
      status_changed_callbacks_;
  bool observing_memory_saver_ = false;
  bool observing_download_manager_ = false;
  bool browser_collection_retry_posted_ = false;
  bool shutting_down_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ResourcePolicyService> weak_ptr_factory_{this};
};

}  // namespace ahoi::resource_policy

#endif  // AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_SERVICE_H_
