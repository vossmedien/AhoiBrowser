// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_MEDIA_AHOI_MEDIA_STATE_H_
#define AHOI_BROWSER_MEDIA_AHOI_MEDIA_STATE_H_

#include <optional>

#include "ahoi/browser/media/tab_activity_state.h"
#include "base/callback_list.h"
#include "components/tabs/public/tab_alert.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class WebContents;
}

namespace ahoi {

// The small, browser-owned media state consumed by Ahoi tab/sidebar surfaces.
// It deliberately contains no WebContents pointer, media player identifiers,
// renderer state, or timers. `capture_activity` is a projection of the
// existing Chromium TabAlertController; it does not introduce a second
// permission or capture state machine.
struct AhoiMediaState {
  bool audio_playing = false;
  bool audio_muted = false;
  // True when muting should be presented as an active tab alert. Chromium
  // limits this to muted tabs that were recently audible.
  bool audio_muting = false;
  bool picture_in_picture = false;
  AhoiTabActivityState capture_activity;
  std::optional<tabs::TabAlert> primary_alert;

  static AhoiMediaState FromSignals(
      bool audio_playing,
      bool audio_muted,
      bool recently_audible,
      bool picture_in_picture,
      std::optional<tabs::TabAlert> chromium_alert = std::nullopt);

  // Resolves capture, PiP, muted, and audible states using Chromium's alert
  // priority. Other Chromium tab alerts map to no Ahoi media indicator.
  static std::optional<tabs::TabAlert> PrimaryAlertFor(
      const AhoiTabActivityState& capture_activity,
      bool audio_playing,
      bool audio_muting,
      bool picture_in_picture);

  bool operator==(const AhoiMediaState&) const = default;
};

// Deduplicates state transitions and only notifies subscribers when the
// compact state actually changes. This pure model keeps the observer and UI
// layers independently testable.
class AhoiMediaStateModel {
 public:
  using StateChangedCallback =
      base::RepeatingCallback<void(const AhoiMediaState&)>;

  AhoiMediaStateModel();
  AhoiMediaStateModel(const AhoiMediaStateModel&) = delete;
  AhoiMediaStateModel& operator=(const AhoiMediaStateModel&) = delete;
  ~AhoiMediaStateModel();

  base::CallbackListSubscription AddStateChangedCallback(
      StateChangedCallback callback);

  // Returns true if the state changed and subscribers were notified.
  bool Update(AhoiMediaState state);
  bool Reset();

  const AhoiMediaState& state() const { return state_; }

 private:
  AhoiMediaState state_;
  base::RepeatingCallbackList<void(const AhoiMediaState&)>
      state_changed_callbacks_;
};

// Observes one WebContents and projects only browser-level media signals into
// AhoiMediaState. WebContentsObserver handles detachment and destruction; the
// state object itself never retains a WebContents pointer.
class AhoiMediaStateTracker final : public content::WebContentsObserver {
 public:
  explicit AhoiMediaStateTracker(content::WebContents* web_contents = nullptr);
  AhoiMediaStateTracker(const AhoiMediaStateTracker&) = delete;
  AhoiMediaStateTracker& operator=(const AhoiMediaStateTracker&) = delete;
  ~AhoiMediaStateTracker() override;

  void SetWebContents(content::WebContents* web_contents);
  void Refresh();

  bool IsTracking(content::WebContents* web_contents) const;

  base::CallbackListSubscription AddStateChangedCallback(
      AhoiMediaStateModel::StateChangedCallback callback);

  const AhoiMediaState& state() const { return model_.state(); }

  // content::WebContentsObserver:
  void OnAudioStateChanged(bool audible) override;
  void DidUpdateAudioMutingState(bool muted) override;
  void MediaPictureInPictureChanged(bool is_picture_in_picture) override;
  void WebContentsDestroyed() override;

 private:
  void OnRecentlyAudibleChanged(bool was_recently_audible);

  AhoiMediaStateModel model_;
  base::CallbackListSubscription recently_audible_subscription_;
  base::CallbackListSubscription tab_alert_subscription_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_MEDIA_AHOI_MEDIA_STATE_H_
