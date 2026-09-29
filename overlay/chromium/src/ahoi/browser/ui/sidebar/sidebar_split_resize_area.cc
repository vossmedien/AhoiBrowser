// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_split_resize_area.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "ahoi/browser/ui/visual_style.h"
#include "cc/paint/paint_flags.h"
#include "chrome/grit/generated_resources.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/gesture_event_details.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace ahoi::sidebar {

namespace {

constexpr double kMinimumRatio = 0.1;
constexpr double kMaximumRatio = 0.9;
constexpr double kKeyboardRatioStep = 0.05;
constexpr std::array<double, 5> kSnapRatios = {0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0,
                                               0.75};

double SnapRatio(double ratio, int ratio_extent) {
  if (ratio_extent <= 0) {
    return std::clamp(ratio, kMinimumRatio, kMaximumRatio);
  }
  const double snap_distance = 6.0 / static_cast<double>(ratio_extent);
  for (const double snap_ratio : kSnapRatios) {
    if (std::abs(ratio - snap_ratio) <= snap_distance) {
      return snap_ratio;
    }
  }
  return std::clamp(ratio, kMinimumRatio, kMaximumRatio);
}

}  // namespace

SidebarSplitResizeArea::SidebarSplitResizeArea(
    SidebarSplitDivider divider,
    SidebarSplitResizeCallback callback)
    : views::ResizeArea(this),
      divider_(std::move(divider)),
      callback_(std::move(callback)),
      initial_ratio_(divider_.ratio) {
  set_axis(divider_.resizes_horizontally() ? Axis::kHorizontal
                                           : Axis::kVertical);
  SetFocusBehavior(FocusBehavior::ALWAYS);
  SetNotifyEnterExitOnChild(true);
  GetViewAccessibility().SetRole(ax::mojom::Role::kSlider);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_ACCNAME_SPLIT_TABS_RESIZE));
  GetViewAccessibility().SetMinValueForRange(
      static_cast<float>(kMinimumRatio * 100.0));
  GetViewAccessibility().SetMaxValueForRange(
      static_cast<float>(kMaximumRatio * 100.0));
  UpdateAccessibilityValue();
}

SidebarSplitResizeArea::~SidebarSplitResizeArea() = default;

void SidebarSplitResizeArea::UpdateConfiguration(
    SidebarSplitDivider divider,
    SidebarSplitResizeCallback callback) {
  // ResizeArea reports every pointer position relative to the mouse-down
  // snapshot. Never replace that baseline while an in-flight ratio update is
  // relaying layout back from TabStripModel.
  if (!is_resizing()) {
    divider_ = std::move(divider);
    callback_ = std::move(callback);
    initial_ratio_ = divider_.ratio;
    set_axis(divider_.resizes_horizontally() ? Axis::kHorizontal
                                             : Axis::kVertical);
    UpdateAccessibilityValue();
  }
}

void SidebarSplitResizeArea::OnResize(int resize_amount, bool done_resizing) {
  // ResizeArea stores its initial pointer coordinate in screen space, but its
  // capture-loss path feeds that value back through the local-to-screen delta
  // conversion a second time. Keep the base call so it clears is_resizing(),
  // while treating that synthetic final update as the intended zero-delta
  // rollback to the ratio captured on pointer-down.
  const double ratio = capture_loss_in_progress_ && done_resizing
                           ? initial_ratio_
                           : RatioForResizeAmount(resize_amount);
  CommitRatio(ratio, done_resizing);
  SchedulePaint();
}

void SidebarSplitResizeArea::OnGestureEvent(ui::GestureEvent* event) {
  if (event->type() == ui::EventType::kGestureTapDown) {
    CaptureInitialRatio();
  }
  views::ResizeArea::OnGestureEvent(event);
}

bool SidebarSplitResizeArea::OnMousePressed(const ui::MouseEvent& event) {
  CaptureInitialRatio();
  const bool handled = views::ResizeArea::OnMousePressed(event);
  SchedulePaint();
  return handled;
}

void SidebarSplitResizeArea::OnMouseReleased(const ui::MouseEvent& event) {
  views::ResizeArea::OnMouseReleased(event);
  SchedulePaint();
}

void SidebarSplitResizeArea::OnMouseCaptureLost() {
  capture_loss_in_progress_ = true;
  views::ResizeArea::OnMouseCaptureLost();
  capture_loss_in_progress_ = false;
  SchedulePaint();
}

void SidebarSplitResizeArea::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  SchedulePaint();
  views::View::OnMouseEntered(event);
}

void SidebarSplitResizeArea::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  SchedulePaint();
  views::View::OnMouseExited(event);
}

bool SidebarSplitResizeArea::OnKeyPressed(const ui::KeyEvent& event) {
  int physical_direction = 0;
  if (axis() == Axis::kHorizontal) {
    physical_direction = event.key_code() == ui::VKEY_RIGHT  ? 1
                         : event.key_code() == ui::VKEY_LEFT ? -1
                                                             : 0;
  } else {
    physical_direction = event.key_code() == ui::VKEY_DOWN ? 1
                         : event.key_code() == ui::VKEY_UP ? -1
                                                           : 0;
  }
  if (physical_direction == 0) {
    return views::ResizeArea::OnKeyPressed(event);
  }
  const double ratio_direction = divider_.reverse_ratio_direction
                                     ? -physical_direction
                                     : physical_direction;
  return CommitRatio(divider_.ratio + ratio_direction * kKeyboardRatioStep,
                     /*done_resizing=*/true);
}

bool SidebarSplitResizeArea::HandleAccessibleAction(
    const ui::AXActionData& action_data) {
  if (action_data.action == ax::mojom::Action::kIncrement) {
    return CommitRatio(divider_.ratio + kKeyboardRatioStep,
                       /*done_resizing=*/true);
  }
  if (action_data.action == ax::mojom::Action::kDecrement) {
    return CommitRatio(divider_.ratio - kKeyboardRatioStep,
                       /*done_resizing=*/true);
  }
  return views::ResizeArea::HandleAccessibleAction(action_data);
}

void SidebarSplitResizeArea::OnPaint(gfx::Canvas* canvas) {
  views::ResizeArea::OnPaint(canvas);
  if (!hovered_ && !is_resizing() && !HasFocus()) {
    return;
  }
  cc::PaintFlags line;
  line.setAntiAlias(true);
  line.setColor(GetColorProvider()->GetColor(visual_style::kAccent));
  line.setStrokeCap(cc::PaintFlags::kRound_Cap);
  line.setStrokeWidth(is_resizing() ? 2.5f : 2.0f);
  line.setStyle(cc::PaintFlags::kStroke_Style);
  if (axis() == Axis::kHorizontal) {
    const float x = static_cast<float>(width()) / 2.0f;
    canvas->DrawLine(gfx::PointF(x, 5.0f),
                     gfx::PointF(x, std::max(5.0f, height() - 5.0f)), line);
  } else {
    const float y = static_cast<float>(height()) / 2.0f;
    canvas->DrawLine(gfx::PointF(5.0f, y),
                     gfx::PointF(std::max(5.0f, width() - 5.0f), y), line);
  }
}

bool SidebarSplitResizeArea::CommitRatio(double ratio, bool done_resizing) {
  const double snapped = SnapRatio(ratio, divider_.ratio_extent);
  if (!callback_ ||
      !callback_.Run(divider_.divider_index, snapped, done_resizing)) {
    return false;
  }
  divider_.ratio = snapped;
  if (done_resizing) {
    initial_ratio_ = snapped;
  }
  UpdateAccessibilityValue();
  return true;
}

double SidebarSplitResizeArea::RatioForResizeAmount(int resize_amount) const {
  if (divider_.ratio_extent <= 0) {
    return initial_ratio_;
  }
  const double direction = divider_.reverse_ratio_direction ? -1.0 : 1.0;
  return initial_ratio_ + direction * static_cast<double>(resize_amount) /
                              static_cast<double>(divider_.ratio_extent);
}

void SidebarSplitResizeArea::CaptureInitialRatio() {
  initial_ratio_ = divider_.ratio;
}

void SidebarSplitResizeArea::UpdateAccessibilityValue() {
  GetViewAccessibility().SetValueForRange(
      static_cast<float>(divider_.ratio * 100.0));
}

BEGIN_METADATA(SidebarSplitResizeArea)
END_METADATA

}  // namespace ahoi::sidebar
