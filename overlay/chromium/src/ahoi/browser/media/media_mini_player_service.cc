// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/media/media_mini_player_service.h"

#include <algorithm>
#include <tuple>
#include <utility>

namespace ahoi {

MediaMiniPlayerService::MediaMiniPlayerService(
    MediaMiniPlayerActionAdapter* action_adapter)
    : action_adapter_(action_adapter) {
  state_ = BuildState();
}

MediaMiniPlayerService::~MediaMiniPlayerService() = default;

base::CallbackListSubscription MediaMiniPlayerService::AddStateChangedCallback(
    StateChangedCallback callback) {
  return state_changed_callbacks_.Add(std::move(callback));
}

void MediaMiniPlayerService::SetActionAdapter(
    MediaMiniPlayerActionAdapter* action_adapter) {
  action_adapter_ = action_adapter;
}

bool MediaMiniPlayerService::RegisterSource(MediaMiniPlayerSource source) {
  if (source.id.empty() || sources_.contains(source.id)) {
    return false;
  }

  const MediaMiniPlayerSourceId source_id = source.id;
  sources_.emplace(source_id, std::move(source));
  const MediaMiniPlayerSource* const selected =
      selected_source_ ? FindSource(*selected_source_) : nullptr;
  if (!selected_source_ ||
      (!selected ||
       (!selected->IsRelevant() && sources_.at(source_id).IsRelevant())) ||
      (sources_.at(source_id).playback ==
           MediaMiniPlayerPlaybackState::kPlaying &&
       selected->playback != MediaMiniPlayerPlaybackState::kPlaying)) {
    selected_source_ = source_id;
  }
  ReconcileDisplayModeWithSelectedSource();
  return NotifyIfChanged();
}

bool MediaMiniPlayerService::UpdateSource(MediaMiniPlayerSource source) {
  if (source.id.empty()) {
    return false;
  }
  auto source_it = sources_.find(source.id);
  if (source_it == sources_.end()) {
    return false;
  }
  if (source_it->second == source) {
    return false;
  }
  const bool selected_source_stopped_playing =
      selected_source_ && *selected_source_ == source_it->first &&
      source_it->second.playback == MediaMiniPlayerPlaybackState::kPlaying &&
      source.playback != MediaMiniPlayerPlaybackState::kPlaying;
  const bool source_started_playing =
      source_it->second.playback != MediaMiniPlayerPlaybackState::kPlaying &&
      source.playback == MediaMiniPlayerPlaybackState::kPlaying;
  source_it->second = std::move(source);
  const MediaMiniPlayerSource* const selected =
      selected_source_ ? FindSource(*selected_source_) : nullptr;
  if (selected_source_ && *selected_source_ == source_it->first &&
      !source_it->second.IsRelevant()) {
    SelectFallbackSource();
  } else if (!selected_source_ ||
             (!selected ||
              (!selected->IsRelevant() && source_it->second.IsRelevant()))) {
    selected_source_ = source_it->first;
  } else if (source_started_playing &&
             selected->playback != MediaMiniPlayerPlaybackState::kPlaying) {
    // Follow the source that actually began playback instead of leaving the
    // compact player attached to an older, merely resumable paused session.
    selected_source_ = source_it->first;
  } else if (selected_source_stopped_playing) {
    // If another tab is still playing, keep the always-visible controls on
    // that active sound source. With no playing alternative the paused source
    // remains selected so the user can resume it directly.
    for (const MediaMiniPlayerSource* candidate :
         SourcesInPresentationOrder()) {
      if (candidate->playback == MediaMiniPlayerPlaybackState::kPlaying) {
        selected_source_ = candidate->id;
        break;
      }
    }
  }
  ReconcileDisplayModeWithSelectedSource();
  return NotifyIfChanged();
}

bool MediaMiniPlayerService::UnregisterSource(
    const MediaMiniPlayerSourceId& source_id) {
  if (sources_.erase(source_id) == 0) {
    return false;
  }

  if (selected_source_ && *selected_source_ == source_id) {
    selected_source_.reset();
    SelectFallbackSource();
  }
  ReconcileDisplayModeWithSelectedSource();
  return NotifyIfChanged();
}

bool MediaMiniPlayerService::SelectSource(
    const MediaMiniPlayerSourceId& source_id) {
  if (!sources_.contains(source_id) ||
      (selected_source_ && *selected_source_ == source_id)) {
    return false;
  }
  selected_source_ = source_id;
  ReconcileDisplayModeWithSelectedSource();
  return NotifyIfChanged();
}

bool MediaMiniPlayerService::SelectNextSource() {
  return SelectRelativeSource(1);
}

bool MediaMiniPlayerService::SelectPreviousSource() {
  return SelectRelativeSource(-1);
}

bool MediaMiniPlayerService::HasSource(
    const MediaMiniPlayerSourceId& source_id) const {
  return FindSource(source_id) != nullptr;
}

bool MediaMiniPlayerService::HasMultipleRelevantSources() const {
  size_t relevant_count = 0;
  for (const auto& [source_id, source] : sources_) {
    if (source.IsRelevant() && ++relevant_count > 1) {
      return true;
    }
  }
  return false;
}

bool MediaMiniPlayerService::DispatchPlayPause(
    const MediaMiniPlayerSourceId& source_id) {
  const MediaMiniPlayerSource* const source = FindSource(source_id);
  if (!source || !source->capabilities.can_play_pause || !action_adapter_) {
    return false;
  }
  const bool should_play =
      source->playback != MediaMiniPlayerPlaybackState::kPlaying;
  return action_adapter_->SetPlaying(source_id, should_play);
}

bool MediaMiniPlayerService::DispatchMute(
    const MediaMiniPlayerSourceId& source_id) {
  const MediaMiniPlayerSource* const source = FindSource(source_id);
  if (!source || !source->capabilities.can_mute || !action_adapter_) {
    return false;
  }
  return action_adapter_->SetMuted(source_id, !source->is_muted);
}

bool MediaMiniPlayerService::DispatchPictureInPicture(
    const MediaMiniPlayerSourceId& source_id) {
  const MediaMiniPlayerSource* const source = FindSource(source_id);
  if (!source || !source->capabilities.can_picture_in_picture ||
      !action_adapter_) {
    return false;
  }
  return action_adapter_->SetPictureInPicture(
      source_id, !source->is_in_picture_in_picture);
}

bool MediaMiniPlayerService::DispatchSeek(
    const MediaMiniPlayerSourceId& source_id,
    base::TimeDelta position) {
  const MediaMiniPlayerSource* const source = FindSource(source_id);
  if (!source || !source->capabilities.can_seek || !action_adapter_ ||
      position.is_negative() || position.is_inf()) {
    return false;
  }
  if (source->duration.is_positive()) {
    position = std::min(position, source->duration);
  }
  return action_adapter_->Seek(source_id, position);
}

bool MediaMiniPlayerService::NotifyIfChanged() {
  MediaMiniPlayerState next_state = BuildState();
  if (state_ == next_state) {
    return false;
  }
  state_ = std::move(next_state);
  state_changed_callbacks_.Notify(state_);
  return true;
}

bool MediaMiniPlayerService::SelectRelativeSource(int delta) {
  if (delta == 0) {
    return false;
  }

  std::vector<MediaMiniPlayerSourceId> relevant_sources;
  for (const MediaMiniPlayerSource* source : SourcesInPresentationOrder()) {
    if (source->IsRelevant()) {
      relevant_sources.push_back(source->id);
    }
  }
  if (relevant_sources.empty()) {
    return false;
  }

  size_t current_index = 0;
  bool current_is_relevant = false;
  if (selected_source_) {
    for (size_t index = 0; index < relevant_sources.size(); ++index) {
      if (relevant_sources[index] == *selected_source_) {
        current_index = index;
        current_is_relevant = true;
        break;
      }
    }
  }

  size_t target_index = 0;
  if (!current_is_relevant) {
    target_index = delta > 0 ? 0 : relevant_sources.size() - 1;
  } else if (delta > 0) {
    target_index = (current_index + 1) % relevant_sources.size();
  } else {
    target_index =
        current_index == 0 ? relevant_sources.size() - 1 : current_index - 1;
  }
  if (selected_source_ && *selected_source_ == relevant_sources[target_index]) {
    return false;
  }
  selected_source_ = relevant_sources[target_index];
  ReconcileDisplayModeWithSelectedSource();
  return NotifyIfChanged();
}

void MediaMiniPlayerService::ReconcileDisplayModeWithSelectedSource() {
  const MediaMiniPlayerSource* const selected =
      selected_source_ ? FindSource(*selected_source_) : nullptr;
  if (!selected || !selected->IsRelevant()) {
    display_mode_ = MediaMiniPlayerDisplayMode::kMiniPlayer;
    return;
  }
  display_mode_ = selected->is_in_picture_in_picture
                      ? MediaMiniPlayerDisplayMode::kPictureInPicture
                      : MediaMiniPlayerDisplayMode::kMiniPlayer;
}

void MediaMiniPlayerService::SelectFallbackSource() {
  for (const MediaMiniPlayerSource* source : SourcesInPresentationOrder()) {
    if (source->playback == MediaMiniPlayerPlaybackState::kPlaying) {
      selected_source_ = source->id;
      return;
    }
  }
  for (const MediaMiniPlayerSource* source : SourcesInPresentationOrder()) {
    if (source->IsRelevant()) {
      selected_source_ = source->id;
      return;
    }
  }
  const std::vector<const MediaMiniPlayerSource*> ordered_sources =
      SourcesInPresentationOrder();
  if (!ordered_sources.empty()) {
    selected_source_ = ordered_sources.front()->id;
  }
}

std::vector<const MediaMiniPlayerSource*>
MediaMiniPlayerService::SourcesInPresentationOrder() const {
  std::vector<const MediaMiniPlayerSource*> ordered_sources;
  ordered_sources.reserve(sources_.size());
  for (const auto& [source_id, source] : sources_) {
    ordered_sources.push_back(&source);
  }
  std::ranges::sort(ordered_sources, [](const MediaMiniPlayerSource* lhs,
                                        const MediaMiniPlayerSource* rhs) {
    return std::tie(lhs->presentation_order, lhs->id) <
           std::tie(rhs->presentation_order, rhs->id);
  });
  return ordered_sources;
}

MediaMiniPlayerState MediaMiniPlayerService::BuildState() const {
  MediaMiniPlayerState state;
  state.selected_source = selected_source_;
  state.display_mode = display_mode_;
  state.sources.reserve(sources_.size());
  for (const MediaMiniPlayerSource* source : SourcesInPresentationOrder()) {
    state.sources.push_back(*source);
  }
  return state;
}

const MediaMiniPlayerSource* MediaMiniPlayerService::FindSource(
    const MediaMiniPlayerSourceId& source_id) const {
  const auto source_it = sources_.find(source_id);
  return source_it == sources_.end() ? nullptr : &source_it->second;
}

}  // namespace ahoi
