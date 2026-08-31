// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_RESIZE_AREA_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_RESIZE_AREA_H_

#include <cstddef>

#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "base/functional/callback.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/controls/resize_area_delegate.h"

namespace ui {
struct AXActionData;
class GestureEvent;
class KeyEvent;
class MouseEvent;
}  // namespace ui

namespace ahoi::sidebar {

// Returns true only after the Chromium-owned split ratio accepted the update.
// `done_resizing` is false during pointer motion and true for the final commit.
using SidebarSplitResizeCallback = base::RepeatingCallback<
    bool(size_t divider_index, double ratio, bool done_resizing)>;

// A generous, transparent native pointer surface around one quiet sidebar
// separator. It updates the same primary/secondary SplitTabVisualData ratio as
// MultiContentsView and never owns a second split model.
class SidebarSplitResizeArea final : public views::ResizeArea,
                                     public views::ResizeAreaDelegate {
  METADATA_HEADER(SidebarSplitResizeArea, views::ResizeArea)

 public:
  SidebarSplitResizeArea(SidebarSplitDivider divider,
                         SidebarSplitResizeCallback callback);
  SidebarSplitResizeArea(const SidebarSplitResizeArea&) = delete;
  SidebarSplitResizeArea& operator=(const SidebarSplitResizeArea&) = delete;
  ~SidebarSplitResizeArea() override;

  void UpdateConfiguration(SidebarSplitDivider divider,
                           SidebarSplitResizeCallback callback);
  size_t divider_index() const { return divider_.divider_index; }

  // views::ResizeAreaDelegate:
  void OnResize(int resize_amount, bool done_resizing) override;

  // views::View:
  void OnGestureEvent(ui::GestureEvent* event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool HandleAccessibleAction(const ui::AXActionData& action_data) override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  bool CommitRatio(double ratio, bool done_resizing);
  double RatioForResizeAmount(int resize_amount) const;
  void CaptureInitialRatio();
  void UpdateAccessibilityValue();

  SidebarSplitDivider divider_;
  SidebarSplitResizeCallback callback_;
  double initial_ratio_ = 0.5;
  bool capture_loss_in_progress_ = false;
  bool hovered_ = false;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_SPLIT_RESIZE_AREA_H_
