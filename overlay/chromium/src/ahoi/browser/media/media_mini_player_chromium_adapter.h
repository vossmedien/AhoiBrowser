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
