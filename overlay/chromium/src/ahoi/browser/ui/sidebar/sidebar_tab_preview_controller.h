// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_PREVIEW_CONTROLLER_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_PREVIEW_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view_tracker.h"

namespace views {
class BubbleDialogDelegate;
class ImageView;
class View;
class Widget;
}  // namespace views

namespace ahoi::sidebar {

struct SidebarTabPreviewTarget {
  enum class Kind {
    kSavedPage,
    kRuntimeTab,
  };

  static SidebarTabPreviewTarget SavedPage(base::Uuid node_id);
  static SidebarTabPreviewTarget RuntimeTab(int tab_handle);

  Kind kind = Kind::kRuntimeTab;
  base::Uuid saved_node_id;
  int runtime_tab_handle = -1;

  bool operator==(const SidebarTabPreviewTarget&) const = default;
};

struct SidebarTabPreviewData {
  std::u16string title;
  gfx::ImageSkia favicon;
  std::vector<gfx::ImageSkia> thumbnails;
};

// Owns the one passive hover surface for a browser sidebar. Stable target
// identity and anchor validation are intentionally centralized here because
// saved rows are virtualized and runtime rows may be rebuilt asynchronously.
class SidebarTabPreviewController final {
 public:
  using ResolveDataCallback =
      base::RepeatingCallback<std::optional<SidebarTabPreviewData>(
          const SidebarTabPreviewTarget&)>;
  using ValidateAnchorCallback =
      base::RepeatingCallback<bool(const SidebarTabPreviewTarget&,
                                   const views::View*)>;

  SidebarTabPreviewController(ResolveDataCallback resolve_data,
                              ValidateAnchorCallback validate_anchor);
  SidebarTabPreviewController(const SidebarTabPreviewController&) = delete;
  SidebarTabPreviewController& operator=(const SidebarTabPreviewController&) =
      delete;
  ~SidebarTabPreviewController();

  void OnSavedPageHover(const base::Uuid& node_id,
                        views::View* anchor,
                        bool hovered);
  void OnRuntimeTabHover(int tab_handle, views::View* anchor, bool hovered);
  void Refresh();
  void Hide();

 private:
  void Request(SidebarTabPreviewTarget target,
               views::View* anchor,
               bool hovered);
  bool HasValidCurrentAnchor() const;
  void ShowAfterDelay();
  void UpdateBubble(const SidebarTabPreviewData& data);
  void CloseBubble();
  void OnBubbleClosed();

  const ResolveDataCallback resolve_data_;
  const ValidateAnchorCallback validate_anchor_;
  std::optional<SidebarTabPreviewTarget> target_;
  views::ViewTracker anchor_tracker_;
  base::OneShotTimer show_timer_;
  raw_ptr<views::ImageView> preview_image_view_ = nullptr;
  std::unique_ptr<views::BubbleDialogDelegate> bubble_delegate_;
  std::unique_ptr<views::Widget> bubble_widget_;
  base::WeakPtrFactory<SidebarTabPreviewController> weak_ptr_factory_{this};
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_PREVIEW_CONTROLLER_H_
