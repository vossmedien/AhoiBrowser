// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/popup/popup_overlay_layout.h"

#include <algorithm>

#include "ahoi/browser/ui/visual_style.h"

namespace ahoi::popup_ui {

PopupOverlayLayout CalculatePopupOverlayLayout(
    const gfx::Rect& available_bounds,
    const gfx::Size& requested_card_size) {
  const int action_rail_width =
      std::min(visual_style::kPopupActionRailWidth, available_bounds.width());
  const int available_card_width =
      std::max(0, available_bounds.width() - action_rail_width -
                      visual_style::kPopupActionRailGap -
                      2 * visual_style::kPopupOverlayInset);
  const int available_card_height = std::max(
      0, available_bounds.height() - 2 * visual_style::kPopupOverlayInset);

  const int desired_width = requested_card_size.width() > 0
                                ? requested_card_size.width()
                                : visual_style::kPopupCardWidth;
  const int desired_height = requested_card_size.height() > 0
                                 ? requested_card_size.height()
                                 : visual_style::kPopupCardHeight;
  const int card_width =
      available_card_width >= visual_style::kPopupCardMinimumWidth
          ? std::clamp(desired_width, visual_style::kPopupCardMinimumWidth,
                       available_card_width)
          : available_card_width;
  const int card_height =
      available_card_height >= visual_style::kPopupCardMinimumHeight
          ? std::clamp(desired_height, visual_style::kPopupCardMinimumHeight,
                       available_card_height)
          : available_card_height;

  // Once a pane becomes too narrow to render any card pixels, center the
  // clamped action rail itself instead of retaining an empty external gap.
  const int action_rail_gap =
      card_width > 0
          ? std::min(visual_style::kPopupActionRailGap,
                     available_bounds.width() - action_rail_width - card_width)
          : 0;
  const int total_width = card_width + action_rail_gap + action_rail_width;
  const int origin_x =
      available_bounds.x() +
      std::max(0, (available_bounds.width() - total_width) / 2);
  const int origin_y =
      available_bounds.y() +
      std::max(0, (available_bounds.height() - card_height) / 2);

  return {
      .card_bounds = gfx::Rect(origin_x, origin_y, card_width, card_height),
      .action_rail_bounds = gfx::Rect(
          origin_x + card_width + action_rail_gap,
          origin_y +
              std::max(
                  0, (card_height - visual_style::kPopupActionRailHeight) / 2),
          action_rail_width,
          std::min(card_height, visual_style::kPopupActionRailHeight)),
  };
}

}  // namespace ahoi::popup_ui
