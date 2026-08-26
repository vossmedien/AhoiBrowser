// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_OVERLAY_VIEW_H_
#define AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_OVERLAY_VIEW_H_

#include <memory>
#include <optional>

#include "ahoi/browser/ui/split_drop/split_drop_intent.h"
#include "ui/views/view.h"

namespace ahoi::split_drop {

// Paint-only overlay. BrowserView may attach it as an ignored-layout child
// whose bounds match MultiContentsView; it never participates in hit testing.
class SplitDropOverlayView final : public views::View {
  METADATA_HEADER(SplitDropOverlayView, views::View)

 public:
  SplitDropOverlayView();
  SplitDropOverlayView(const SplitDropOverlayView&) = delete;
  SplitDropOverlayView& operator=(const SplitDropOverlayView&) = delete;
  ~SplitDropOverlayView() override;

  void SetIntent(const DropIntent& intent);
  void ClearIntent();
  const std::optional<DropIntent>& intent_for_testing() const {
    return intent_;
  }

  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

 private:
  std::optional<DropIntent> intent_;
};

std::unique_ptr<SplitDropOverlayView> CreateSplitDropOverlayView();

}  // namespace ahoi::split_drop

#endif  // AHOI_BROWSER_UI_SPLIT_DROP_SPLIT_DROP_OVERLAY_VIEW_H_
