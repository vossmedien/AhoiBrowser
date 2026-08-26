// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "ahoi/browser/resource_policy/resource_policy_service.h"
#include "ahoi/browser/resource_policy/resource_policy_service_internal.h"
#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/performance_manager/public/user_tuning/user_performance_tuning_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/login/login_tab_helper.h"
#include "chrome/browser/ui/recently_audible_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/permissions/permission_request_manager.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/media_session/public/cpp/media_metadata.h"

namespace ahoi::resource_policy {

namespace {

bool MediaSessionNeedsProtection(
    const media_session::mojom::MediaSessionInfo& info) {
  using MediaPlaybackState = media_session::mojom::MediaPlaybackState;
  using MediaSessionState =
      media_session::mojom::MediaSessionInfo::SessionState;
  return info.playback_state == MediaPlaybackState::kPlaying ||
         info.is_controllable || info.state == MediaSessionState::kActive ||
         info.state == MediaSessionState::kDucking || info.has_presentation ||
         info.picture_in_picture_state ==
             media_session::mojom::MediaPictureInPictureState::
                 kInPictureInPicture;
}

}  // namespace

class ResourcePolicyService::TabSignalsObserver final
    : public content::WebContentsObserver,
      public permissions::PermissionRequestManager::Observer,
      public media_session::mojom::MediaSessionObserver {
 public:
  TabSignalsObserver(ResourcePolicyService* owner, tabs::TabInterface* tab)
      : content::WebContentsObserver(tab ? tab->GetContents() : nullptr),
        owner_(owner ? owner->weak_ptr_factory_.GetWeakPtr()
                     : base::WeakPtr<ResourcePolicyService>()),
        tab_(tab ? tab->GetWeakPtr() : base::WeakPtr<tabs::TabInterface>()) {
    ObservePermissionManager();
    BindMediaSessionIfPresent();
  }

  TabSignalsObserver(const TabSignalsObserver&) = delete;
  TabSignalsObserver& operator=(const TabSignalsObserver&) = delete;

  ~TabSignalsObserver() override {
    if (permission_manager_) {
      permission_manager_->RemoveObserver(this);
    }
  }

  bool media_session_needs_protection() const {
    return media_session_needs_protection_;
  }

  bool media_session_picture_in_picture() const {
    return media_session_picture_in_picture_;
  }

  bool permission_request_in_progress() const {
    return permission_manager_ && permission_manager_->IsRequestInProgress();
  }

  void SetWebContents(content::WebContents* contents) {
    if (permission_manager_) {
      permission_manager_->RemoveObserver(this);
      permission_manager_ = nullptr;
    }
    media_session_observer_receiver_.reset();
    media_session_needs_protection_ = false;
    media_session_picture_in_picture_ = false;
    Observe(contents);
    ObservePermissionManager();
    BindMediaSessionIfPresent();
  }

  // WebContentsObserver:
  void DidStartLoading() override {
    if (owner_ && tab_ && web_contents() && web_contents()->WasDiscarded()) {
      owner_->SetWaking(tab_.get(), true);
    } else {
      RefreshOwner();
    }
  }

  void DidStopLoading() override {
    if (owner_ && tab_) {
      owner_->SetWaking(tab_.get(), false);
    }
  }

  void LoadProgressChanged(double) override { RefreshOwner(); }
  void DidGetUserInteraction(const blink::WebInputEvent&) override {
    RefreshOwner();
  }
  void BeforeUnloadFired(bool) override { RefreshOwner(); }
  void BeforeUnloadDialogCancelled() override { RefreshOwner(); }
  void OnFrameIsCapturingMediaStreamChanged(content::RenderFrameHost*,
                                            bool) override {
    RefreshOwner();
  }
  void DidFinishNavigation(content::NavigationHandle*) override {
    ObservePermissionManager();
    RefreshOwner();
    // LoginTabHelper may create the HTTP-auth prompt later in the same
    // DidFinishNavigation observer pass. Re-read once after that event has
    // unwound; this is event-driven and never polls.
    RefreshOwnerSoon();
  }
  void PrimaryPageChanged(content::Page&) override {
    media_session_observer_receiver_.reset();
    media_session_needs_protection_ = false;
    media_session_picture_in_picture_ = false;
    BindMediaSessionIfPresent();
    RefreshOwner();
  }
  void OnAudioStateChanged(bool) override { RefreshOwner(); }
  void DidUpdateAudioMutingState(bool) override { RefreshOwner(); }
  void MediaPictureInPictureChanged(bool) override { RefreshOwner(); }
  void MediaSessionCreated(content::MediaSession* media_session) override {
    BindMediaSession(media_session);
    RefreshOwner();
  }
  void WebContentsDestroyed() override {
    if (permission_manager_) {
      permission_manager_->RemoveObserver(this);
      permission_manager_ = nullptr;
    }
    media_session_observer_receiver_.reset();
    media_session_needs_protection_ = false;
    media_session_picture_in_picture_ = false;
  }

  // PermissionRequestManager::Observer:
  void OnPromptAdded() override { RefreshOwner(); }
  void OnPromptRemoved() override { RefreshOwner(); }
  void OnRequestsFinalized() override { RefreshOwner(); }
  void OnPermissionRequestManagerDestructed() override {
    permission_manager_ = nullptr;
    RefreshOwner();
  }

  // MediaSessionObserver:
  void MediaSessionInfoChanged(
      media_session::mojom::MediaSessionInfoPtr info) override {
    media_session_needs_protection_ =
        info && MediaSessionNeedsProtection(*info);
    media_session_picture_in_picture_ =
        info && info->picture_in_picture_state ==
                    media_session::mojom::MediaPictureInPictureState::
                        kInPictureInPicture;
    RefreshOwner();
  }
  void MediaSessionMetadataChanged(
      const std::optional<media_session::MediaMetadata>&) override {}
  void MediaSessionActionsChanged(
      const std::vector<media_session::mojom::MediaSessionAction>&) override {}
  void MediaSessionImagesChanged(
      const base::flat_map<media_session::mojom::MediaSessionImageType,
                           std::vector<media_session::MediaImage>>&) override {}
  void MediaSessionPositionChanged(
      const std::optional<media_session::MediaPosition>&) override {}

 private:
  void RefreshOwner() {
    if (owner_ && tab_) {
      owner_->RefreshTab(tab_.get());
    }
  }

  void RefreshOwnerSoon() {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<ResourcePolicyService> owner,
                          base::WeakPtr<tabs::TabInterface> tab) {
                         if (owner && tab) {
                           owner->RefreshTab(tab.get());
                         }
                       },
                       owner_, tab_));
  }

  void ObservePermissionManager() {
    auto* manager =
        web_contents() ? permissions::PermissionRequestManager::FromWebContents(
                             web_contents())
                       : nullptr;
    if (manager == permission_manager_) {
      return;
    }
    if (permission_manager_) {
      permission_manager_->RemoveObserver(this);
    }
    permission_manager_ = manager;
    if (permission_manager_) {
      permission_manager_->AddObserver(this);
    }
  }

  void BindMediaSessionIfPresent() {
    BindMediaSession(web_contents()
                         ? content::MediaSession::GetIfExists(web_contents())
                         : nullptr);
  }

  void BindMediaSession(content::MediaSession* media_session) {
    media_session_observer_receiver_.reset();
    if (!media_session) {
      return;
    }
    if (auto info = media_session->GetMediaSessionInfoSync()) {
      media_session_needs_protection_ = MediaSessionNeedsProtection(*info);
      media_session_picture_in_picture_ =
          info->picture_in_picture_state ==
          media_session::mojom::MediaPictureInPictureState::kInPictureInPicture;
    }
    media_session->AddObserver(
        media_session_observer_receiver_.BindNewPipeAndPassRemote());
  }

  base::WeakPtr<ResourcePolicyService> owner_;
  base::WeakPtr<tabs::TabInterface> tab_;
  raw_ptr<permissions::PermissionRequestManager> permission_manager_ = nullptr;
  mojo::Receiver<media_session::mojom::MediaSessionObserver>
      media_session_observer_receiver_{this};
  bool media_session_needs_protection_ = false;
  bool media_session_picture_in_picture_ = false;
};

ResourcePolicyService::TrackedTab::TrackedTab(tabs::TabInterface* tab)
    : tab(tab ? tab->GetWeakPtr() : base::WeakPtr<tabs::TabInterface>()) {}

ResourcePolicyService::TrackedTab::~TrackedTab() = default;

bool ResourcePolicyService::TrackedTab::HasExplicitProtection() const {
  return std::ranges::any_of(protection_counts,
                             [](size_t count) { return count > 0; });
}

bool ResourcePolicyService::TrackedTab::HasExplicitProtection(
    CriticalFlow flow) const {
  const size_t index = static_cast<size_t>(flow);
  return index < protection_counts.size() && protection_counts[index] > 0;
}

void ResourcePolicyService::InitializeRuntime() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !profile_) {
    return;
  }
  if (!observing_memory_saver_ &&
      performance_manager::user_tuning::UserPerformanceTuningManager::
          HasInstance()) {
    performance_manager::user_tuning::UserPerformanceTuningManager::
        GetInstance()
            ->AddObserver(this);
    observing_memory_saver_ = true;
  }
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile_);
  if (collection && !browser_collection_observation_.IsObserving()) {
    browser_collection_observation_.Observe(collection);
    collection->ForEach(
        [this](BrowserWindowInterface* browser) {
          TrackBrowser(browser);
          return true;
        },
        BrowserCollection::Order::kCreation,
        /*enumerate_new_browsers=*/true);
  } else if (!collection && !browser_collection_retry_posted_) {
    // Eager profile services can be constructed immediately before the
    // collection. Retry once on the UI sequence; never poll or schedule idle
    // work if the collection is unavailable for a non-window profile.
    browser_collection_retry_posted_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ResourcePolicyService::InitializeRuntime,
                                  weak_ptr_factory_.GetWeakPtr()));
  }

  if (!download_manager_) {
    download_manager_ = profile_->GetDownloadManager();
  }
  if (download_manager_ && !observing_download_manager_) {
    download_manager_->AddObserver(this);
    observing_download_manager_ = true;
    content::DownloadManager::DownloadVector downloads;
    download_manager_->GetAllDownloads(&downloads);
    for (download::DownloadItem* item : downloads) {
      OnDownloadCreated(download_manager_, item);
    }
  }
}

void ResourcePolicyService::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  weak_ptr_factory_.InvalidateWeakPtrs();
  profile_observation_.Reset();
  if (observing_memory_saver_ &&
      performance_manager::user_tuning::UserPerformanceTuningManager::
          HasInstance()) {
    performance_manager::user_tuning::UserPerformanceTuningManager::
        GetInstance()
            ->RemoveObserver(this);
  }
  observing_memory_saver_ = false;
  browser_collection_observation_.Reset();
  TabStripModelObserver::StopObservingAll(this);
  for (download::DownloadItem* item : observed_downloads_) {
    item->RemoveObserver(this);
  }
  observed_downloads_.clear();
  download_in_progress_.clear();
  if (download_manager_ && observing_download_manager_) {
    download_manager_->RemoveObserver(this);
  }
  observing_download_manager_ = false;
  download_manager_ = nullptr;
  tabs_.clear();
  models_.clear();
  windows_.clear();
  profile_ = nullptr;
}

bool ResourcePolicyService::ShouldTrackBrowser(
    const BrowserWindowInterface* browser) const {
  return !shutting_down_ && profile_ && browser &&
         browser->GetProfile() == profile_ &&
         browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
         !browser->IsDeleteScheduled() && browser->GetTabStripModel();
}

void ResourcePolicyService::TrackBrowser(BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ShouldTrackBrowser(browser) || windows_.contains(browser)) {
    return;
  }
  TabStripModel* model = browser->GetTabStripModel();
  if (models_.contains(model)) {
    return;
  }
  windows_.emplace(browser, model);
  models_.emplace(model, browser);
  model->AddObserver(this);
  for (tabs::TabInterface* tab : *model) {
    TrackTab(model, tab);
  }
}

void ResourcePolicyService::UntrackBrowser(BrowserWindowInterface* browser,
                                           bool model_destroyed) {
  auto window = windows_.find(browser);
  if (window == windows_.end()) {
    return;
  }
  TabStripModel* model = window->second;
  if (model && !model_destroyed) {
    model->RemoveObserver(this);
  }
  std::vector<tabs::TabInterface*> remove;
  for (const auto& [tab, tracked] : tabs_) {
    if (tracked->model == model) {
      remove.push_back(tab);
    }
  }
  for (tabs::TabInterface* tab : remove) {
    UntrackTab(tab);
  }
  models_.erase(model);
  windows_.erase(window);
}

void ResourcePolicyService::TrackTab(TabStripModel* model,
                                     tabs::TabInterface* tab) {
  if (!model || !tab || tab->GetProfile() != profile_) {
    return;
  }
  auto [it, inserted] = tabs_.try_emplace(tab);
  if (inserted) {
    it->second = std::make_unique<TrackedTab>(tab);
    TrackedTab& tracked = *it->second;
    tracked.signals = std::make_unique<TabSignalsObserver>(this, tab);
    tracked.will_discard_contents_subscription =
        tab->RegisterWillDiscardContents(base::BindRepeating(
            [](base::WeakPtr<ResourcePolicyService> service,
               tabs::TabInterface* changed_tab, content::WebContents*,
               content::WebContents*) {
              base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](base::WeakPtr<ResourcePolicyService> inner_service,
                         base::WeakPtr<tabs::TabInterface> inner_tab) {
                        if (inner_service && inner_tab) {
                          inner_service->UpdateTrackedContents(inner_tab.get());
                        }
                      },
                      service, changed_tab->GetWeakPtr()));
            },
            weak_ptr_factory_.GetWeakPtr()));
    tracked.did_activate_subscription =
        tab->RegisterDidActivate(base::BindRepeating(
            [](base::WeakPtr<ResourcePolicyService> service,
               tabs::TabInterface* changed_tab) {
              if (service) {
                service->RefreshTab(changed_tab);
              }
            },
            weak_ptr_factory_.GetWeakPtr()));
    tracked.will_deactivate_subscription =
        tab->RegisterWillDeactivate(base::BindRepeating(
            [](base::WeakPtr<ResourcePolicyService> service,
               tabs::TabInterface* changed_tab) {
              if (service) {
                service->RefreshTab(changed_tab);
              }
            },
            weak_ptr_factory_.GetWeakPtr()));
    tracked.modal_ui_subscription =
        tab->RegisterModalUIChanged(base::BindRepeating(
            [](base::WeakPtr<ResourcePolicyService> service,
               tabs::TabInterface* changed_tab) {
              if (service) {
                service->RefreshTab(changed_tab);
              }
            },
            weak_ptr_factory_.GetWeakPtr()));
    tracked.blocked_state_subscription =
        tab->RegisterBlockedStateChanged(base::BindRepeating(
            [](base::WeakPtr<ResourcePolicyService> service,
               tabs::TabInterface* changed_tab, bool) {
              if (service) {
                service->RefreshTab(changed_tab);
              }
            },
            weak_ptr_factory_.GetWeakPtr()));
  }
  it->second->model = model;
  UpdateTrackedContents(tab);
}

void ResourcePolicyService::UntrackTab(tabs::TabInterface* tab) {
  auto it = tabs_.find(tab);
  if (it == tabs_.end()) {
    return;
  }
  TrackedTab& tracked = *it->second;
  if (tracked.ahoi_auto_discard_block_applied &&
      tracked.auto_discardable_before_ahoi_block && tab && tab->GetContents()) {
    if (auto* lifecycle =
            resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
                tab->GetContents())) {
      lifecycle->SetAutoDiscardable(true);
    }
  }
  tabs_.erase(it);
}

void ResourcePolicyService::UpdateTrackedContents(tabs::TabInterface* tab) {
  auto it = tabs_.find(tab);
  if (it == tabs_.end()) {
    return;
  }
  it->second->signals->SetWebContents(tab->GetContents());
  RefreshTab(tab);
}

void ResourcePolicyService::RefreshTab(tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = tabs_.find(tab);
  if (shutting_down_ || it == tabs_.end() || !it->second->tab) {
    return;
  }
  const CriticalSignals signals = CollectCriticalSignals(tab);
  ApplyAutomaticProtection(tab, signals);
  const TabResourceStatus status = GetTabStatus(tab);
  TrackedTab& tracked = *it->second;
  if (!tracked.has_published_status ||
      tracked.last_published_status != status) {
    tracked.last_published_status = status;
    tracked.has_published_status = true;
    status_changed_callbacks_.Notify(tab, status);
  }
}

void ResourcePolicyService::RefreshAllTabs() {
  std::vector<tabs::TabInterface*> tabs;
  tabs.reserve(tabs_.size());
  for (const auto& [tab, tracked] : tabs_) {
    if (tracked->tab) {
      tabs.push_back(tab);
    }
  }
  for (tabs::TabInterface* tab : tabs) {
    RefreshTab(tab);
  }
}

void ResourcePolicyService::SetWaking(tabs::TabInterface* tab, bool waking) {
  auto it = tabs_.find(tab);
  if (it == tabs_.end() || it->second->waking == waking) {
    return;
  }
  it->second->waking = waking;
  RefreshTab(tab);
}

CriticalSignals ResourcePolicyService::CollectCriticalSignals(
    tabs::TabInterface* tab) const {
  CriticalSignals signals;
  if (!tab || tab->GetProfile() != profile_ || !tab->GetContents()) {
    signals.upstream_protected = true;
    return signals;
  }
  content::WebContents* contents = tab->GetContents();
  signals.active_pane = tab->IsActivated();
  signals.visible_pane =
      contents->GetVisibility() == content::Visibility::VISIBLE;
  signals.audible = contents->IsCurrentlyAudible();
  if (RecentlyAudibleHelper* helper =
          RecentlyAudibleHelper::FromWebContents(contents)) {
    signals.recently_audible = helper->WasRecentlyAudible();
  }
  signals.picture_in_picture = contents->HasPictureInPictureVideo() ||
                               contents->HasPictureInPictureDocument();
  signals.capture = contents->IsBeingCaptured();
  signals.download = HasInProgressDownload(tab);
  const uint64_t upload_size = contents->GetUploadSize();
  signals.upload =
      upload_size > 0 && contents->GetUploadPosition() < upload_size;
  signals.before_unload = contents->NeedToFireBeforeUnloadOrUnloadEvents();
  signals.modal_flow = !tab->CanShowModalUI() || tab->IsBlocked();
  if (LoginTabHelper* login = LoginTabHelper::FromWebContents(contents)) {
    signals.http_auth = login->IsShowingPrompt();
  }

  const auto it = tabs_.find(tab);
  if (it != tabs_.end()) {
    const TrackedTab& tracked = *it->second;
    signals.media_session = tracked.signals->media_session_needs_protection();
    signals.picture_in_picture |=
        tracked.signals->media_session_picture_in_picture();
    signals.permission_prompt =
        tracked.signals->permission_request_in_progress() ||
        tracked.HasExplicitProtection(CriticalFlow::kPermissionPrompt);
    signals.file_chooser =
        tracked.HasExplicitProtection(CriticalFlow::kFileChooser);
    signals.http_auth |= tracked.HasExplicitProtection(CriticalFlow::kHttpAuth);
    signals.before_unload |=
        tracked.HasExplicitProtection(CriticalFlow::kBeforeUnload);
    signals.download |= tracked.HasExplicitProtection(CriticalFlow::kDownload);
    signals.upload |= tracked.HasExplicitProtection(CriticalFlow::kUpload);
    signals.modal_flow |=
        tracked.HasExplicitProtection(CriticalFlow::kOtherModal);
    signals.product_protection = tracked.HasExplicitProtection();
    signals.media_session |=
        tracked.HasExplicitProtection(CriticalFlow::kMiniPlayer);
  } else if (auto* media_session =
                 content::MediaSession::GetIfExists(contents)) {
    if (auto info = media_session->GetMediaSessionInfoSync()) {
      signals.media_session = MediaSessionNeedsProtection(*info);
    }
  }
  signals.never_sleep = IsNeverSleep(tab);
  return signals;
}

bool ResourcePolicyService::HasInProgressDownload(
    tabs::TabInterface* tab) const {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents || !download_manager_) {
    return false;
  }
  for (download::DownloadItem* item : observed_downloads_) {
    if (!item || item->IsDone()) {
      continue;
    }
    if (content::DownloadItemUtils::GetOriginalWebContents(item) == contents ||
        content::DownloadItemUtils::GetWebContents(item) == contents) {
      return true;
    }
  }
  return false;
}

void ResourcePolicyService::OnBrowserCreated(BrowserWindowInterface* browser) {
  TrackBrowser(browser);
}

void ResourcePolicyService::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (!shutting_down_) {
    UntrackBrowser(browser, /*model_destroyed=*/false);
  }
}

void ResourcePolicyService::OnTabStripModelChanged(
    TabStripModel* model,
    const TabStripModelChange& change,
    const TabStripSelectionChange&) {
  if (shutting_down_ || !models_.contains(model)) {
    return;
  }
  switch (change.type()) {
    case TabStripModelChange::kSelectionOnly:
      break;
    case TabStripModelChange::kInserted:
      for (const auto& inserted : change.GetInsert()->contents) {
        TrackTab(model, inserted.tab);
      }
      break;
    case TabStripModelChange::kRemoved:
      for (const auto& removed : change.GetRemove()->contents) {
        if (removed.tab_detach_reason ==
            tabs::TabInterface::DetachReason::kDelete) {
          UntrackTab(removed.tab);
        } else if (auto it = tabs_.find(removed.tab); it != tabs_.end()) {
          it->second->model = nullptr;
        }
      }
      break;
    case TabStripModelChange::kMoved:
      TrackTab(model, change.GetMove()->tab);
      break;
    case TabStripModelChange::kReplaced:
      TrackTab(model, change.GetReplace()->tab);
      break;
  }
  RefreshAllTabs();
}

void ResourcePolicyService::OnTabStripModelDestroyed(TabStripModel* model) {
  if (auto it = models_.find(model); it != models_.end()) {
    UntrackBrowser(it->second, /*model_destroyed=*/true);
  }
}

void ResourcePolicyService::OnDownloadCreated(content::DownloadManager*,
                                              download::DownloadItem* item) {
  if (!item || observed_downloads_.contains(item)) {
    return;
  }
  observed_downloads_.insert(item);
  download_in_progress_[item] = !item->IsDone();
  item->AddObserver(this);
  RefreshAllTabs();
}

void ResourcePolicyService::ManagerGoingDown(
    content::DownloadManager* manager) {
  if (manager != download_manager_) {
    return;
  }
  for (download::DownloadItem* item : observed_downloads_) {
    item->RemoveObserver(this);
  }
  observed_downloads_.clear();
  download_in_progress_.clear();
  observing_download_manager_ = false;
  download_manager_ = nullptr;
  RefreshAllTabs();
}

void ResourcePolicyService::OnDownloadUpdated(download::DownloadItem* item) {
  if (!item) {
    return;
  }
  const bool in_progress = !item->IsDone();
  auto [it, inserted] = download_in_progress_.try_emplace(item, in_progress);
  if (!inserted && it->second == in_progress) {
    return;
  }
  it->second = in_progress;
  RefreshAllTabs();
}

void ResourcePolicyService::OnDownloadRemoved(download::DownloadItem* item) {
  auto it = download_in_progress_.find(item);
  if (it != download_in_progress_.end() && it->second) {
    it->second = false;
    RefreshAllTabs();
  }
}

void ResourcePolicyService::OnDownloadDestroyed(download::DownloadItem* item) {
  const auto activity = download_in_progress_.find(item);
  const bool was_in_progress =
      activity != download_in_progress_.end() && activity->second;
  download_in_progress_.erase(item);
  if (!observed_downloads_.erase(item)) {
    return;
  }
  if (was_in_progress) {
    RefreshAllTabs();
  }
}

}  // namespace ahoi::resource_policy
