// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_MEDIA_MEDIA_MINI_PLAYER_TYPES_H_
#define AHOI_BROWSER_MEDIA_MEDIA_MINI_PLAYER_TYPES_H_

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "url/origin.h"

namespace ahoi {

using MediaMiniPlayerSourceId = std::string;

enum class MediaMiniPlayerPlaybackState {
  kPaused = 0,
  kPlaying = 1,
  kBuffering = 2,
  kEnded = 3,
};

// Runtime presentation ownership for the selected media source. Chromium's
// observed PiP state is authoritative; compact/expanded MiniPlayer appearance
// is persisted separately by the host.
enum class MediaMiniPlayerDisplayMode {
  kMiniPlayer = 0,
  kPictureInPicture = 1,
};

struct MediaMiniPlayerCapabilities {
  bool can_play_pause = false;
  bool can_mute = false;
  bool can_picture_in_picture = false;
  bool can_seek = false;

  bool operator==(const MediaMiniPlayerCapabilities&) const = default;
};

// UI-neutral snapshot for one active media session. It contains no
// WebContents, MediaSession, renderer, or view pointer; an integration layer
// can translate its own session callbacks into this value.
struct MediaMiniPlayerSource {
  MediaMiniPlayerSourceId id;
  // Browser-owned visual order (normally TabStripModel order). The service
  // uses this for explicit previous/next source navigation and deterministic
  // fallback; the renderer cannot influence it.
  int presentation_order = 0;
  std::u16string title;
  url::Origin origin;
  MediaMiniPlayerPlaybackState playback = MediaMiniPlayerPlaybackState::kPaused;
  bool is_muted = false;
  bool is_in_picture_in_picture = false;
  base::TimeDelta position;
  base::TimeDelta duration;
  // MediaPosition snapshots are projected while the visible scrubber is
  // active. No service timer is needed, and paused/hidden surfaces stay idle.
  double playback_rate = 0.0;
  base::TimeTicks position_updated_at;
  MediaMiniPlayerCapabilities capabilities;

  base::TimeDelta PositionAt(base::TimeTicks now) const {
    base::TimeDelta current = position;
    if (playback == MediaMiniPlayerPlaybackState::kPlaying &&
        playback_rate > 0.0 && !position_updated_at.is_null() &&
        now > position_updated_at) {
      current += playback_rate * (now - position_updated_at);
    }
    current = std::max(base::TimeDelta(), current);
    return duration.is_positive() ? std::min(current, duration) : current;
  }

  // A registered tab is not necessarily a media source yet. Chromium tabs
  // can be registered before their MediaSession exists; the UI should remain
  // hidden until the source exposes a usable media signal or action.
  bool IsRelevant() const {
    return playback != MediaMiniPlayerPlaybackState::kPaused ||
           is_in_picture_in_picture || capabilities.can_play_pause ||
           capabilities.can_mute || capabilities.can_picture_in_picture ||
           capabilities.can_seek || duration.is_positive();
  }

  bool operator==(const MediaMiniPlayerSource&) const = default;
};

// Immutable-at-the-boundary service snapshot. The vector follows the
// browser-owned presentation order with source ID as a stable tie-breaker.
struct MediaMiniPlayerState {
  std::vector<MediaMiniPlayerSource> sources;
  std::optional<MediaMiniPlayerSourceId> selected_source;
  MediaMiniPlayerDisplayMode display_mode =
      MediaMiniPlayerDisplayMode::kMiniPlayer;

  bool operator==(const MediaMiniPlayerState&) const = default;
};

// Browser integration implements this adapter to bridge actions to the
// corresponding MediaSession/WebContents. The service never assumes how the
// underlying session is represented and never performs optimistic state
// updates; the next source snapshot remains authoritative.
class MediaMiniPlayerActionAdapter {
 public:
  virtual ~MediaMiniPlayerActionAdapter() = default;

  virtual bool SetPlaying(const MediaMiniPlayerSourceId& source_id,
                          bool playing) = 0;
  virtual bool SetMuted(const MediaMiniPlayerSourceId& source_id,
                        bool muted) = 0;
  virtual bool SetPictureInPicture(const MediaMiniPlayerSourceId& source_id,
                                   bool in_picture_in_picture) = 0;
  virtual bool Seek(const MediaMiniPlayerSourceId& source_id,
                    base::TimeDelta position) = 0;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_MEDIA_MEDIA_MINI_PLAYER_TYPES_H_
