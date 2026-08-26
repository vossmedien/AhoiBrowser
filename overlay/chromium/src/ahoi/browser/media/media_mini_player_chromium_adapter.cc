// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/media/media_mini_player_chromium_adapter.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/page.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/media_session/public/cpp/media_metadata.h"

namespace ahoi {
namespace {

using MediaSessionAction = media_session::mojom::MediaSessionAction;

bool HasAction(const std::vector<MediaSessionAction>& actions,
               MediaSessionAction wanted_action) {
  return std::ranges::find(actions, wanted_action) != actions.end();
}

}  // namespace

// static
MediaMiniPlayerCapabilities
MediaMiniPlayerChromiumAdapter::CapabilitiesForActions(
    const std::vector<MediaSessionAction>& actions) {
  return {
      .can_play_pause = HasAction(actions, MediaSessionAction::kPlay) ||
                        HasAction(actions, MediaSessionAction::kPause),
      .can_mute = HasAction(actions, MediaSessionAction::kSetMute),
      .can_picture_in_picture =
          HasAction(actions, MediaSessionAction::kEnterPictureInPicture) ||
          HasAction(actions, MediaSessionAction::kExitPictureInPicture),
      .can_seek = HasAction(actions, MediaSessionAction::kSeekTo) ||
                  HasAction(actions, MediaSessionAction::kScrubTo),
  };
}

// static
MediaMiniPlayerPlaybackState MediaMiniPlayerChromiumAdapter::PlaybackStateFor(
    media_session::mojom::MediaPlaybackState playback_state) {
  return playback_state == media_session::mojom::MediaPlaybackState::kPlaying
             ? MediaMiniPlayerPlaybackState::kPlaying
             : MediaMiniPlayerPlaybackState::kPaused;
}

class MediaMiniPlayerChromiumAdapter::SourceObserver final
    : public content::WebContentsObserver,
      public media_session::mojom::MediaSessionObserver {
 public:
  SourceObserver(MediaMiniPlayerChromiumAdapter& owner,
                 const MediaMiniPlayerSourceId& source_id,
                 content::WebContents* web_contents,
                 int presentation_order)
      : content::WebContentsObserver(web_contents),
        owner_(owner),
        source_id_(source_id) {
    source_.id = source_id_;
    source_.presentation_order = presentation_order;
    BindMediaSessionIfPresent();
    RefreshSource();
  }

  SourceObserver(const SourceObserver&) = delete;
  SourceObserver& operator=(const SourceObserver&) = delete;
  ~SourceObserver() override = default;

  void Stop() {
    media_session_observer_receiver_.reset();
    Observe(nullptr);
    web_contents_destroyed_ = true;
  }

  bool UpdateWebContents(content::WebContents* web_contents,
                         int presentation_order) {
    if (!web_contents) {
      return false;
    }
    bool changed = false;
    if (source_.presentation_order != presentation_order) {
      source_.presentation_order = presentation_order;
      changed = true;
    }
    if (this->web_contents() == web_contents && !web_contents_destroyed_) {
      if (changed) {
        RefreshSource();
      }
      return changed;
    }
    media_session_observer_receiver_.reset();
    ResetMediaSessionState();
    web_contents_destroyed_ = false;
    Observe(web_contents);
    BindMediaSessionIfPresent();
    RefreshSource();
    return true;
  }

  bool SetPlaying(bool playing) {
    content::MediaSession* const media_session = GetMediaSession();
    if (!media_session) {
      return false;
    }
    const MediaSessionAction action =
        playing ? MediaSessionAction::kPlay : MediaSessionAction::kPause;
    if (!HasAction(actions_, action)) {
      return false;
    }
    if (playing) {
      media_session->Resume(content::MediaSession::SuspendType::kUI);
    } else {
      media_session->Suspend(content::MediaSession::SuspendType::kUI);
    }
    return true;
  }

  bool SetMuted(bool muted) {
    if (web_contents_destroyed_ || !web_contents()) {
      return false;
    }
    content::MediaSession* const media_session = GetMediaSession();
    bool dispatched = false;
    if (!muted) {
      // A page may be muted at both Chromium layers (MediaSession player and
      // tab output). Clearing both avoids a misleading second "unmute" click.
      if (media_session && session_muted_ &&
          HasAction(actions_, MediaSessionAction::kSetMute)) {
        media_session->SetMute(false);
        dispatched = true;
      }
      if (web_contents()->IsAudioMuted()) {
        dispatched |= SetTabAudioMuted(web_contents(), false,
                                       TabMutedReason::kAudioIndicator,
                                       /*extension_id=*/std::string());
      }
      return dispatched;
    }
    if (media_session && HasAction(actions_, MediaSessionAction::kSetMute)) {
      media_session->SetMute(true);
      return true;
    }
    return SetTabAudioMuted(web_contents(), true,
                            TabMutedReason::kAudioIndicator,
                            /*extension_id=*/std::string());
  }

  bool SetPictureInPicture(bool in_picture_in_picture) {
    content::MediaSession* const media_session = GetMediaSession();
    if (!media_session) {
      return false;
    }
    const MediaSessionAction action =
        in_picture_in_picture ? MediaSessionAction::kEnterPictureInPicture
                              : MediaSessionAction::kExitPictureInPicture;
    if (!HasAction(actions_, action)) {
      return false;
    }
    if (in_picture_in_picture) {
      media_session->EnterPictureInPicture();
    } else {
      media_session->ExitPictureInPicture();
    }
    return true;
  }

  bool Seek(base::TimeDelta position) {
    if (position.is_negative() || position.is_inf()) {
      return false;
    }
    content::MediaSession* const media_session = GetMediaSession();
    if (!media_session) {
      return false;
    }
    if (HasAction(actions_, MediaSessionAction::kSeekTo)) {
      media_session->SeekTo(position);
      return true;
    }
    if (HasAction(actions_, MediaSessionAction::kScrubTo)) {
      // ScrubTo is the only remaining public absolute-position API. The UI
      // can follow this with a final SeekTo once the page registers it.
      media_session->ScrubTo(position);
      return true;
    }
    return false;
  }

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override {
    media_session_observer_receiver_.reset();
    ResetMediaSessionState();
    BindMediaSessionIfPresent();
    RefreshSource();
  }

  void TitleWasSet(content::NavigationEntry*) override { RefreshSource(); }

  void OnAudioStateChanged(bool audible) override { RefreshSource(); }

  void DidUpdateAudioMutingState(bool muted) override { RefreshSource(); }

  void MediaPictureInPictureChanged(bool is_picture_in_picture) override {
    RefreshSource();
  }

  void AboutToBeDiscarded(content::WebContents* new_contents) override {
    media_session_observer_receiver_.reset();
    ResetMediaSessionState();
    web_contents_destroyed_ = false;
    Observe(new_contents);
    BindMediaSessionIfPresent();
    RefreshSource();
  }

  void MediaSessionCreated(content::MediaSession* media_session) override {
    BindMediaSession(media_session);
    RefreshSource();
  }

  void WebContentsDestroyed() override {
    web_contents_destroyed_ = true;
    media_session_observer_receiver_.reset();
    // The owning adapter retains this dormant observer until its explicit
    // UnregisterWebContents call, avoiding self-destruction during Chromium's
    // WebContents observer walk.
    owner_->OnSourceWebContentsDestroyed(source_id_);
  }

  // media_session::mojom::MediaSessionObserver:
  void MediaSessionInfoChanged(
      media_session::mojom::MediaSessionInfoPtr session_info) override {
    if (!session_info) {
      ResetMediaSessionState();
    } else {
      has_session_info_ = true;
      session_playback_ = PlaybackStateFor(session_info->playback_state);
      session_muted_ = session_info->muted;
      session_picture_in_picture_ =
          session_info->picture_in_picture_state ==
          media_session::mojom::MediaPictureInPictureState::kInPictureInPicture;
    }
    RefreshSource();
  }

  void MediaSessionMetadataChanged(
      const std::optional<media_session::MediaMetadata>& metadata) override {
    metadata_title_.clear();
    if (metadata) {
      metadata_title_ =
          metadata->title.empty() ? metadata->source_title : metadata->title;
    }
    RefreshSource();
  }

  void MediaSessionActionsChanged(
      const std::vector<MediaSessionAction>& actions) override {
    actions_ = actions;
    RefreshSource();
  }

  void MediaSessionImagesChanged(
      const base::flat_map<media_session::mojom::MediaSessionImageType,
                           std::vector<media_session::MediaImage>>& images)
      override {}

  void MediaSessionPositionChanged(
      const std::optional<media_session::MediaPosition>& position) override {
    has_position_ = position.has_value();
    if (position) {
      position_ = position->GetPosition();
      duration_ = position->duration();
      playback_rate_ = position->playback_rate();
      position_updated_at_ = base::TimeTicks::Now();
    } else {
      position_ = base::TimeDelta();
      duration_ = base::TimeDelta();
      playback_rate_ = 0.0;
      position_updated_at_ = base::TimeTicks();
    }
    RefreshSource();
  }

 private:
  content::MediaSession* GetMediaSession() const {
    if (web_contents_destroyed_ || !web_contents()) {
      return nullptr;
    }
    return content::MediaSession::GetIfExists(web_contents());
  }

  void BindMediaSessionIfPresent() { BindMediaSession(GetMediaSession()); }

  void BindMediaSession(content::MediaSession* media_session) {
    media_session_observer_receiver_.reset();
    if (media_session) {
      media_session->AddObserver(
          media_session_observer_receiver_.BindNewPipeAndPassRemote());
      media_session_observer_receiver_.set_disconnect_handler(base::BindOnce(
          &SourceObserver::OnMediaSessionDisconnected, base::Unretained(this)));
    }
  }

  void OnMediaSessionDisconnected() {
    ResetMediaSessionState();
    RefreshSource();
  }

  void ResetMediaSessionState() {
    has_session_info_ = false;
    session_playback_ = MediaMiniPlayerPlaybackState::kPaused;
    session_muted_ = false;
    session_picture_in_picture_ = false;
    metadata_title_.clear();
    actions_.clear();
    has_position_ = false;
    position_ = base::TimeDelta();
    duration_ = base::TimeDelta();
    playback_rate_ = 0.0;
    position_updated_at_ = base::TimeTicks();
  }

  void RefreshSource() {
    if (web_contents_destroyed_ || !web_contents()) {
      return;
    }

    source_.origin = url::Origin::Create(web_contents()->GetLastCommittedURL());
    source_.title =
        metadata_title_.empty() ? web_contents()->GetTitle() : metadata_title_;
    source_.playback = has_session_info_
                           ? session_playback_
                           : (web_contents()->IsCurrentlyAudible()
                                  ? MediaMiniPlayerPlaybackState::kPlaying
                                  : MediaMiniPlayerPlaybackState::kPaused);
    source_.is_muted = session_muted_ || web_contents()->IsAudioMuted();
    source_.is_in_picture_in_picture =
        session_picture_in_picture_ ||
        web_contents()->HasPictureInPictureVideo() ||
        web_contents()->HasPictureInPictureDocument();
    source_.position = has_position_ ? position_ : base::TimeDelta();
    source_.duration = has_position_ ? duration_ : base::TimeDelta();
    source_.playback_rate = has_position_ ? playback_rate_ : 0.0;
    source_.position_updated_at =
        has_position_ ? position_updated_at_ : base::TimeTicks();
    source_.capabilities = CapabilitiesForActions(actions_);
    // Chromium advertises enter/exit and play/pause as distinct actions. Only
    // enable the control when the action matching the current source state is
    // available, so a stale opposite-state capability cannot create a no-op.
    source_.capabilities.can_play_pause =
        source_.playback == MediaMiniPlayerPlaybackState::kPlaying
            ? HasAction(actions_, MediaSessionAction::kPause)
            : HasAction(actions_, MediaSessionAction::kPlay);
    source_.capabilities.can_picture_in_picture =
        source_.is_in_picture_in_picture
            ? HasAction(actions_, MediaSessionAction::kExitPictureInPicture)
            : HasAction(actions_, MediaSessionAction::kEnterPictureInPicture);
    // Prefer MediaSession's player mute and otherwise use Chromium's tab-wide
    // audio action. A page-controlled muted player without kSetMute remains
    // truthfully marked muted, but cannot pretend that tab unmute would alter
    // the page's player state.
    const bool has_media_signal =
        has_session_info_ || web_contents()->IsCurrentlyAudible() ||
        source_.is_in_picture_in_picture || has_position_ || !actions_.empty();
    source_.capabilities.can_mute =
        HasAction(actions_, MediaSessionAction::kSetMute) ||
        web_contents()->IsAudioMuted() || (has_media_signal && !session_muted_);
    if (!owner_->service_->UpdateSource(source_) &&
        !owner_->service_->HasSource(source_id_)) {
      owner_->service_->RegisterSource(source_);
    }
  }

  raw_ref<MediaMiniPlayerChromiumAdapter> owner_;
  const MediaMiniPlayerSourceId source_id_;
  MediaMiniPlayerSource source_;
  mojo::Receiver<media_session::mojom::MediaSessionObserver>
      media_session_observer_receiver_{this};
  std::vector<MediaSessionAction> actions_;
  std::u16string metadata_title_;
  MediaMiniPlayerPlaybackState session_playback_ =
      MediaMiniPlayerPlaybackState::kPaused;
  base::TimeDelta position_;
  base::TimeDelta duration_;
  double playback_rate_ = 0.0;
  base::TimeTicks position_updated_at_;
  bool has_session_info_ = false;
  bool session_muted_ = false;
  bool session_picture_in_picture_ = false;
  bool has_position_ = false;
  bool web_contents_destroyed_ = false;
};

MediaMiniPlayerChromiumAdapter::MediaMiniPlayerChromiumAdapter(
    MediaMiniPlayerService& service)
    : service_(service) {
  service_->SetActionAdapter(this);
}

MediaMiniPlayerChromiumAdapter::~MediaMiniPlayerChromiumAdapter() {
  for (auto& [source_id, observer] : observers_) {
    observer->Stop();
  }
  service_->SetActionAdapter(nullptr);
}

bool MediaMiniPlayerChromiumAdapter::RegisterWebContents(
    const MediaMiniPlayerSourceId& source_id,
    content::WebContents* web_contents,
    int presentation_order) {
  if (source_id.empty() || !web_contents || IsRegistered(source_id)) {
    return false;
  }

  MediaMiniPlayerSource source;
  source.id = source_id;
  source.presentation_order = presentation_order;
  source.origin = url::Origin::Create(web_contents->GetLastCommittedURL());
  source.title = web_contents->GetTitle();
  if (!service_->RegisterSource(source)) {
    return false;
  }

  auto observer = std::make_unique<SourceObserver>(
      *this, source_id, web_contents, presentation_order);
  observers_.emplace(source_id, std::move(observer));
  return true;
}

bool MediaMiniPlayerChromiumAdapter::UnregisterWebContents(
    const MediaMiniPlayerSourceId& source_id) {
  auto observer_it = observers_.find(source_id);
  if (observer_it == observers_.end()) {
    return false;
  }
  observer_it->second->Stop();
  observers_.erase(observer_it);
  service_->UnregisterSource(source_id);
  return true;
}

bool MediaMiniPlayerChromiumAdapter::UpdateWebContents(
    const MediaMiniPlayerSourceId& source_id,
    content::WebContents* web_contents,
    int presentation_order) {
  SourceObserver* const observer = FindObserver(source_id);
  return observer &&
         observer->UpdateWebContents(web_contents, presentation_order);
}

bool MediaMiniPlayerChromiumAdapter::IsRegistered(
    const MediaMiniPlayerSourceId& source_id) const {
  return observers_.contains(source_id);
}

bool MediaMiniPlayerChromiumAdapter::SetPlaying(
    const MediaMiniPlayerSourceId& source_id,
    bool playing) {
  SourceObserver* const observer = FindObserver(source_id);
  return observer && observer->SetPlaying(playing);
}

bool MediaMiniPlayerChromiumAdapter::SetMuted(
    const MediaMiniPlayerSourceId& source_id,
    bool muted) {
  SourceObserver* const observer = FindObserver(source_id);
  return observer && observer->SetMuted(muted);
}

bool MediaMiniPlayerChromiumAdapter::SetPictureInPicture(
    const MediaMiniPlayerSourceId& source_id,
    bool in_picture_in_picture) {
  SourceObserver* const observer = FindObserver(source_id);
  return observer && observer->SetPictureInPicture(in_picture_in_picture);
}

bool MediaMiniPlayerChromiumAdapter::Seek(
    const MediaMiniPlayerSourceId& source_id,
    base::TimeDelta position) {
  SourceObserver* const observer = FindObserver(source_id);
  return observer && observer->Seek(position);
}

void MediaMiniPlayerChromiumAdapter::OnSourceWebContentsDestroyed(
    const MediaMiniPlayerSourceId& source_id) {
  service_->UnregisterSource(source_id);
}

MediaMiniPlayerChromiumAdapter::SourceObserver*
MediaMiniPlayerChromiumAdapter::FindObserver(
    const MediaMiniPlayerSourceId& source_id) {
  const auto observer_it = observers_.find(source_id);
  return observer_it == observers_.end() ? nullptr : observer_it->second.get();
}

}  // namespace ahoi
