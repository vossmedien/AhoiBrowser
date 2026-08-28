// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_media_indicator.h"

#include <algorithm>

#include "ahoi/browser/ui/visual_style.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/gfx/geometry/rect.h"

namespace ahoi::sidebar {

ui::ImageModel GetSidebarMediaIndicator(
    std::optional<tabs::TabAlert> alert_state) {
  if (!alert_state.has_value()) {
    return ui::ImageModel();
  }

  const gfx::VectorIcon* icon = nullptr;
  ui::ColorId color = visual_style::kMutedText;
  switch (*alert_state) {
    case tabs::TabAlert::kAudioPlaying:
      icon = &vector_icons::kVolumeUpIcon;
      break;
    case tabs::TabAlert::kAudioMuting:
      icon = &vector_icons::kVolumeOffIcon;
      break;
    case tabs::TabAlert::kPipPlaying:
      icon = &vector_icons::kPictureInPictureIcon;
      break;
    case tabs::TabAlert::kMediaRecording:
      icon = &vector_icons::kRadioButtonCheckedIcon;
      color = ui::kColorSysError;
      break;
    case tabs::TabAlert::kAudioRecording:
      icon = &vector_icons::kMicIcon;
      color = ui::kColorSysError;
      break;
    case tabs::TabAlert::kVideoRecording:
      icon = &vector_icons::kVideocamIcon;
      color = ui::kColorSysError;
      break;
    case tabs::TabAlert::kTabCapturing:
      icon = &vector_icons::kCaptureIcon;
      color = visual_style::kAccent;
      break;
    case tabs::TabAlert::kDesktopCapturing:
      icon = &vector_icons::kScreenShareIcon;
      color = ui::kColorSysError;
      break;
    default:
      return ui::ImageModel();
  }
  return ui::ImageModel::FromVectorIcon(*icon, color, 16);
}

std::optional<tabs::TabAlert> GetSidebarMediaAlertForSession(
    bool playing,
    bool muted,
    bool picture_in_picture,
    bool relevant) {
  if (picture_in_picture) {
    return tabs::TabAlert::kPipPlaying;
  }
  if (muted && relevant) {
    return tabs::TabAlert::kAudioMuting;
  }
  if (playing) {
    return tabs::TabAlert::kAudioPlaying;
  }
  return std::nullopt;
}

SidebarTabTrailingLayout GetSidebarTabTrailingLayout(int width,
                                                     int height,
                                                     bool has_media_indicator) {
  constexpr int kTitleStart = 30;
  constexpr int kTrailingRightInset = 4;
  constexpr int kSlotWidth = 24;
  constexpr int kSlotGap = 2;
  constexpr int kTitleTrailingGap = 7;
  const gfx::Rect row_bounds(0, 0, std::max(0, width), std::max(0, height));
  gfx::Rect hover_action(std::max(0, width - kTrailingRightInset - kSlotWidth),
                         2, kSlotWidth, std::max(0, height - 4));
  hover_action.Intersect(row_bounds);
  gfx::Rect media_indicator =
      has_media_indicator
          ? gfx::Rect(std::max(0, hover_action.x() - kSlotGap - kSlotWidth), 2,
                      kSlotWidth, std::max(0, height - 4))
          : gfx::Rect();
  media_indicator.Intersect(row_bounds);
  const int title_end =
      (has_media_indicator ? media_indicator.x() : hover_action.x()) -
      kTitleTrailingGap;
  gfx::Rect title(kTitleStart, 0, std::max(0, title_end - kTitleStart), height);
  title.Intersect(row_bounds);
  return {.title = title,
          .media_indicator = media_indicator,
          .hover_action = hover_action};
}

}  // namespace ahoi::sidebar
