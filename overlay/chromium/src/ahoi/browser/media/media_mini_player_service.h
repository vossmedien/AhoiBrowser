// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_MEDIA_MEDIA_MINI_PLAYER_SERVICE_H_
#define AHOI_BROWSER_MEDIA_MEDIA_MINI_PLAYER_SERVICE_H_

#include <map>
#include <optional>

#include "ahoi/browser/media/media_mini_player_types.h"
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"

namespace ahoi {

// Owns the browser-neutral source registry and selection policy for Ahoi's
// MiniPlayer surface. It is deliberately not a view controller or a
// MediaSession implementation.
class MediaMiniPlayerService {
 public:
  using StateChangedCallback =
      base::RepeatingCallback<void(const MediaMiniPlayerState&)>;

  explicit MediaMiniPlayerService(
      MediaMiniPlayerActionAdapter* action_adapter = nullptr);
  MediaMiniPlayerService(const MediaMiniPlayerService&) = delete;
  MediaMiniPlayerService& operator=(const MediaMiniPlayerService&) = delete;
  ~MediaMiniPlayerService();

  base::CallbackListSubscription AddStateChangedCallback(
      StateChangedCallback callback);

  void SetActionAdapter(MediaMiniPlayerActionAdapter* action_adapter);

  // Registration is explicit and rejects empty or duplicate IDs. Updates must
  // use UpdateSource so a source cannot silently change identity.
  bool RegisterSource(MediaMiniPlayerSource source);
  bool UpdateSource(MediaMiniPlayerSource source);
  bool UnregisterSource(const MediaMiniPlayerSourceId& source_id);
  bool SelectSource(const MediaMiniPlayerSourceId& source_id);
  bool SelectNextSource();
  bool SelectPreviousSource();
  bool HasSource(const MediaMiniPlayerSourceId& source_id) const;
  bool HasMultipleRelevantSources() const;

  // The selected source remains selected through updates. If it is removed,
  // selection falls back to the first relevant browser presentation entry.
  const MediaMiniPlayerState& state() const { return state_; }

  // Actions are dispatched only when the source exists, the capability is
  // advertised, and an adapter is installed. State is updated by subsequent
  // source snapshots rather than optimistically here.
  bool DispatchPlayPause(const MediaMiniPlayerSourceId& source_id);
  bool DispatchMute(const MediaMiniPlayerSourceId& source_id);
  bool DispatchPictureInPicture(const MediaMiniPlayerSourceId& source_id);
  bool DispatchSeek(const MediaMiniPlayerSourceId& source_id,
                    base::TimeDelta position);

 private:
  using SourceMap =
      std::map<MediaMiniPlayerSourceId, MediaMiniPlayerSource, std::less<>>;

  bool NotifyIfChanged();
  bool SelectRelativeSource(int delta);
  void ReconcileDisplayModeWithSelectedSource();
  void SelectFallbackSource();
  std::vector<const MediaMiniPlayerSource*> SourcesInPresentationOrder() const;
  MediaMiniPlayerState BuildState() const;
  const MediaMiniPlayerSource* FindSource(
      const MediaMiniPlayerSourceId& source_id) const;

  SourceMap sources_;
  std::optional<MediaMiniPlayerSourceId> selected_source_;
  MediaMiniPlayerDisplayMode display_mode_;
  MediaMiniPlayerState state_;
  raw_ptr<MediaMiniPlayerActionAdapter> action_adapter_ = nullptr;
  base::RepeatingCallbackList<void(const MediaMiniPlayerState&)>
      state_changed_callbacks_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_MEDIA_MEDIA_MINI_PLAYER_SERVICE_H_
