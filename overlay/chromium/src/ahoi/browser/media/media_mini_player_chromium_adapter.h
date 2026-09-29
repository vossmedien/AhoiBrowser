// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_MEDIA_MEDIA_MINI_PLAYER_CHROMIUM_ADAPTER_H_
#define AHOI_BROWSER_MEDIA_MEDIA_MINI_PLAYER_CHROMIUM_ADAPTER_H_

#include <map>
#include <memory>
#include <vector>

#include "ahoi/browser/media/media_mini_player_service.h"
#include "base/memory/raw_ref.h"
#include "services/media_session/public/mojom/media_session.mojom.h"

namespace content {
class WebContents;
}

namespace ahoi {

// Bridges Ahoi's UI-neutral mini-player model to Chromium MediaSession APIs.
// One source observer owns one WebContents observation and one Mojo observer
// connection. The service remains the source of truth for UI snapshots.
class MediaMiniPlayerChromiumAdapter final
    : public MediaMiniPlayerActionAdapter {
 public:
  explicit MediaMiniPlayerChromiumAdapter(MediaMiniPlayerService& service);
  MediaMiniPlayerChromiumAdapter(const MediaMiniPlayerChromiumAdapter&) =
      delete;
  MediaMiniPlayerChromiumAdapter& operator=(
      const MediaMiniPlayerChromiumAdapter&) = delete;
  ~MediaMiniPlayerChromiumAdapter() override;

  // Registration does not create a MediaSession when the page has no media.
  // The source is connected when Chromium reports MediaSessionCreated.
  bool RegisterWebContents(const MediaMiniPlayerSourceId& source_id,
                           content::WebContents* web_contents,
                           int presentation_order = 0);
  // Keeps a stable source ID across a tab's WebContents replacement. This is
  // common during navigation/discard/restore and must not create a duplicate
  // service source or leave actions bound to the old renderer.
  bool UpdateWebContents(const MediaMiniPlayerSourceId& source_id,
                         content::WebContents* web_contents,
                         int presentation_order = 0);
  bool UnregisterWebContents(const MediaMiniPlayerSourceId& source_id);
  bool IsRegistered(const MediaMiniPlayerSourceId& source_id) const;

  // Public pure mappings keep the Chromium-to-Ahoi contract directly
  // testable without requiring a renderer or a browser window.
  static MediaMiniPlayerCapabilities CapabilitiesForActions(
      const std::vector<media_session::mojom::MediaSessionAction>& actions);
  static MediaMiniPlayerPlaybackState PlaybackStateFor(
      media_session::mojom::MediaPlaybackState playback_state);

  // What Chromium reports for one tab: its MediaSession, if any, and the
  // WebContents audio and Picture-in-Picture state.
  struct SessionSignals {
    bool has_session_info = false;
    MediaMiniPlayerPlaybackState session_playback =
        MediaMiniPlayerPlaybackState::kPaused;
    // MediaSessionInfo::is_controllable. Chromium clears it for inactive
    // sessions, one-shot players and transient (5 s or shorter) audio.
    bool session_controllable = false;
    bool session_muted = false;
    bool session_picture_in_picture = false;
    std::vector<media_session::mojom::MediaSessionAction> actions;
    bool tab_audible = false;
    bool tab_muted = false;
    bool tab_picture_in_picture = false;
    // Whether the previous projection owned a card: a controllable session,
    // a PiP window, audible sound without a MediaSession, or a muted card
    // kept from one of those.
    bool was_controllable = false;
    // The tab was unmuted while it had a relevant card, and Chromium has not
    // yet reported the session it re-adds the players to. Unmuting runs
    // synchronously, the new MediaSessionInfo arrives later over Mojo.
    bool awaiting_session_after_unmute = false;
  };

  struct SessionProjection {
    MediaMiniPlayerPlaybackState playback =
        MediaMiniPlayerPlaybackState::kPaused;
    bool is_muted = false;
    bool is_in_picture_in_picture = false;
    // MediaPosition is shown only for sessions that own the card. A short
    // sound's own duration must not make its tab a relevant source.
    bool expose_position = false;
    MediaMiniPlayerCapabilities capabilities;
    // The next SessionSignals::was_controllable value.
    bool keeps_controllable_card = false;
  };

  // Only a session that offers play or pause, a PiP window, or audible
  // sound without a MediaSession owns a card.
  // A notification chime reports kPlaying without being controllable, so it
  // is projected as paused and cannot take the selection from a paused
  // player. A controllable session that becomes muted keeps its card, since
  // Chromium withdraws a muted tab's players and the card is where the user
  // unmutes it; the card also survives the unmute until the session is back.
  // A tab muted before its media started never joins its session, so while
  // it is audible it is offered as a paused source with an unmute control.
  // A session whose media ended is inactive and not controllable; like
  // Chromium's Global Media Controls, it releases the card unless the page
  // keeps its own play handler.
  static SessionProjection ProjectSession(const SessionSignals& signals);

  // MediaMiniPlayerActionAdapter:
  bool SetPlaying(const MediaMiniPlayerSourceId& source_id,
                  bool playing) override;
  bool SetMuted(const MediaMiniPlayerSourceId& source_id, bool muted) override;
  bool SetPictureInPicture(const MediaMiniPlayerSourceId& source_id,
                           bool in_picture_in_picture) override;
  bool Seek(const MediaMiniPlayerSourceId& source_id,
            base::TimeDelta position) override;

 private:
  class SourceObserver;

  void OnSourceWebContentsDestroyed(const MediaMiniPlayerSourceId& source_id);
  SourceObserver* FindObserver(const MediaMiniPlayerSourceId& source_id);

  raw_ref<MediaMiniPlayerService> service_;
  std::map<MediaMiniPlayerSourceId, std::unique_ptr<SourceObserver>> observers_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_MEDIA_MEDIA_MINI_PLAYER_CHROMIUM_ADAPTER_H_
