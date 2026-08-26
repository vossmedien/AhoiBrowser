// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/media/ahoi_media_state.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/ui/recently_audible_helper.h"
#include "chrome/browser/ui/tabs/alert/tab_alert_controller.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace ahoi {

// static
std::optional<tabs::TabAlert> AhoiMediaState::PrimaryAlertFor(
    const AhoiTabActivityState& capture_activity,
    bool audio_playing,
    bool audio_muting,
    bool picture_in_picture) {
  // Chromium ranks capture/recording above playback indicators. Preserve that
  // authority so a microphone, camera, or sharing session cannot be hidden by
  // a simultaneously audible page.
  if (capture_activity.primary_activity.has_value()) {
    return capture_activity.primary_activity;
  }
  // Keep this ordering aligned with tabs::CompareAlerts: PiP is the strongest
  // media signal, followed by muting, then audible playback.
  if (picture_in_picture) {
    return tabs::TabAlert::kPipPlaying;
  }
  if (audio_muting) {
    return tabs::TabAlert::kAudioMuting;
  }
  if (audio_playing) {
    return tabs::TabAlert::kAudioPlaying;
  }
  return std::nullopt;
}

// static
AhoiMediaState AhoiMediaState::FromSignals(
    bool audio_playing,
    bool audio_muted,
    bool recently_audible,
    bool picture_in_picture,
    std::optional<tabs::TabAlert> chromium_alert) {
  AhoiMediaState state;
  state.audio_playing = audio_playing;
  state.audio_muted = audio_muted;
  state.audio_muting = audio_muted && recently_audible;
  state.picture_in_picture = picture_in_picture;
  state.capture_activity =
      AhoiTabActivityState::FromChromiumAlert(chromium_alert);
  state.primary_alert =
      PrimaryAlertFor(state.capture_activity, state.audio_playing,
                      state.audio_muting, state.picture_in_picture);
  return state;
}

AhoiMediaStateModel::AhoiMediaStateModel() = default;
AhoiMediaStateModel::~AhoiMediaStateModel() = default;

base::CallbackListSubscription AhoiMediaStateModel::AddStateChangedCallback(
    StateChangedCallback callback) {
  return state_changed_callbacks_.Add(std::move(callback));
}

bool AhoiMediaStateModel::Update(AhoiMediaState state) {
  if (state_ == state) {
    return false;
  }
  state_ = std::move(state);
  state_changed_callbacks_.Notify(state_);
  return true;
}

bool AhoiMediaStateModel::Reset() {
  return Update(AhoiMediaState());
}

AhoiMediaStateTracker::AhoiMediaStateTracker(
    content::WebContents* web_contents) {
  SetWebContents(web_contents);
}

AhoiMediaStateTracker::~AhoiMediaStateTracker() = default;

void AhoiMediaStateTracker::SetWebContents(content::WebContents* web_contents) {
  recently_audible_subscription_ = {};
  tab_alert_subscription_ = {};
  Observe(web_contents);

  if (web_contents) {
    if (RecentlyAudibleHelper* const helper =
            RecentlyAudibleHelper::FromWebContents(web_contents)) {
      recently_audible_subscription_ =
          helper->RegisterRecentlyAudibleChangedCallback(base::BindRepeating(
              &AhoiMediaStateTracker::OnRecentlyAudibleChanged,
              base::Unretained(this)));
    }
    if (tabs::TabInterface* const tab =
            tabs::TabInterface::MaybeGetFromContents(web_contents)) {
      if (tabs::TabAlertController* const controller =
              tabs::TabAlertController::From(tab)) {
        tab_alert_subscription_ =
            controller->AddAlertToShowChangedCallback(base::BindRepeating(
                [](AhoiMediaStateTracker* tracker,
                   std::optional<tabs::TabAlert>) { tracker->Refresh(); },
                base::Unretained(this)));
      }
    }
  }
  Refresh();
}

bool AhoiMediaStateTracker::IsTracking(
    content::WebContents* web_contents) const {
  return this->web_contents() == web_contents;
}

void AhoiMediaStateTracker::OnRecentlyAudibleChanged(bool) {
  Refresh();
}

void AhoiMediaStateTracker::Refresh() {
  content::WebContents* const contents = web_contents();
  if (!contents) {
    model_.Reset();
    return;
  }

  bool recently_audible = false;
  if (const RecentlyAudibleHelper* const helper =
          RecentlyAudibleHelper::FromWebContents(contents)) {
    recently_audible = helper->WasRecentlyAudible();
  }
  std::optional<tabs::TabAlert> chromium_alert;
  if (tabs::TabInterface* const tab =
          tabs::TabInterface::MaybeGetFromContents(contents)) {
    if (const tabs::TabAlertController* const controller =
            tabs::TabAlertController::From(tab)) {
      chromium_alert = controller->GetAlertToShow();
    }
  }
  model_.Update(
      AhoiMediaState::FromSignals(contents->IsCurrentlyAudible(),
                                  contents->IsAudioMuted(), recently_audible,
                                  contents->HasPictureInPictureVideo() ||
                                      contents->HasPictureInPictureDocument(),
                                  chromium_alert));
}

base::CallbackListSubscription AhoiMediaStateTracker::AddStateChangedCallback(
    AhoiMediaStateModel::StateChangedCallback callback) {
  return model_.AddStateChangedCallback(std::move(callback));
}

void AhoiMediaStateTracker::OnAudioStateChanged(bool audible) {
  Refresh();
}

void AhoiMediaStateTracker::DidUpdateAudioMutingState(bool muted) {
  Refresh();
}

void AhoiMediaStateTracker::MediaPictureInPictureChanged(
    bool is_picture_in_picture) {
  Refresh();
}

void AhoiMediaStateTracker::WebContentsDestroyed() {
  recently_audible_subscription_ = {};
  tab_alert_subscription_ = {};
  // WebContentsObserver detaches itself after dispatching this callback. Do
  // not call Observe(nullptr) here: WebContents destruction is midway through
  // its observer walk and removing ourselves reentrantly is unsafe.
  model_.Reset();
}

}  // namespace ahoi
