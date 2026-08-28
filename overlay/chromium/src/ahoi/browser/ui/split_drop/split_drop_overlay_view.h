// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_OVERLAY_VIEW_H_
#define AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_OVERLAY_VIEW_H_

#include <memory>
#include <optional>

#include "ahoi/browser/ui/split_drop/split_drop_intent.h"
#include "ui/views/view.h"

namespace ahoi::split_drop {

// Paint-only overlay. BrowserView attaches it as an ignored-layout child whose
// bounds match MultiContentsView. It remains event-transparent: on macOS the
// native WebContents destination forwards only Ahoi's private tab drags to the
// Views root, while this View communicates the active target and exact intent.
class SplitDropOverlayView final : public views::View {
  METADATA_HEADER(SplitDropOverlayView, views::View)

 public:
  SplitDropOverlayView();
  SplitDropOverlayView(const SplitDropOverlayView&) = delete;
  SplitDropOverlayView& operator=(const SplitDropOverlayView&) = delete;
  ~SplitDropOverlayView() override;

  // An accepted Ahoi tab drag first activates the complete split surface. A
  // valid intent then adds a stronger, geometry-stable drop-zone highlight.
  void BeginDragPresentation();
  void SetIntent(const DropIntent& intent);
  void ClearIntent();
  void EndDragPresentation();
  bool drag_active_for_testing() const { return drag_active_; }
  const std::optional<DropIntent>& intent_for_testing() const {
    return intent_;
  }

  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

 private:
  bool drag_active_ = false;
  std::optional<DropIntent> intent_;
};

std::unique_ptr<SplitDropOverlayView> CreateSplitDropOverlayView();

}  // namespace ahoi::split_drop

#endif  // AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_OVERLAY_VIEW_H_
