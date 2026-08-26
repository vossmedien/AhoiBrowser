// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_POPUP_POPUP_OVERLAY_VIEW_H_
#define AHOI_BROWSER_UI_POPUP_POPUP_OVERLAY_VIEW_H_

#include <memory>
#include <string>

#include "ahoi/browser/popup/popup_types.h"
#include "ahoi/browser/ui/appearance/appearance_policy.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view.h"
#include "url/origin.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace views {
class ImageButton;
class ImageView;
class Label;
class ViewShadow;
class WebView;
}  // namespace views

namespace ui {
class Accelerator;
}  // namespace ui

namespace ahoi::popup_ui {

// Browser-controlled presentation only. The WebView hosts the exact Chromium
// WebContents owned by PopupOverlayService; it never creates, navigates, or
// substitutes page content.
class PopupOverlayView final : public views::View {
  METADATA_HEADER(PopupOverlayView, views::View)

 public:
  PopupOverlayView(content::BrowserContext* browser_context,
                   base::RepeatingClosure close_callback,
                   base::RepeatingClosure promote_callback,
                   base::RepeatingClosure split_callback,
                   const appearance::GlassPolicy& policy);
  PopupOverlayView(const PopupOverlayView&) = delete;
  PopupOverlayView& operator=(const PopupOverlayView&) = delete;
  ~PopupOverlayView() override;

  void SetWebContents(content::WebContents* contents);
  void SetDisplayedOrigin(const url::Origin& origin);
  void SetAppearance(const appearance::GlassPolicy& policy);
  void SetPreferredCardSize(const gfx::Size& preferred_size);
  void SetSplitAvailability(popup::PopupSplitAvailability availability);
  void ShowStatus(std::u16string message, bool is_error);
  void ClearStatus();
  void FocusWebContents();
  void AnimateIn();
  void AnimateOut(base::OnceClosure completed);

  views::WebView* web_view_for_testing() const { return web_view_; }
  views::View* card_for_testing() const { return card_; }
  views::View* action_rail_for_testing() const { return action_rail_; }
  views::Label* origin_label_for_testing() const { return origin_label_; }
  views::Label* status_label_for_testing() const { return status_label_; }
  views::ImageButton* close_button_for_testing() const { return close_button_; }
  views::ImageButton* promote_button_for_testing() const {
    return promote_button_;
  }
  views::ImageButton* split_button_for_testing() const { return split_button_; }

 private:
  class PopupScrimView;

  // views::View:
  void Layout(PassKey) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

  raw_ptr<views::View> scrim_ = nullptr;
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::View> action_rail_ = nullptr;
  raw_ptr<views::ImageView> origin_icon_ = nullptr;
  raw_ptr<views::Label> origin_label_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<content::WebContents> attached_contents_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<views::ImageButton> promote_button_ = nullptr;
  raw_ptr<views::ImageButton> split_button_ = nullptr;
  std::unique_ptr<views::ViewShadow> card_shadow_;
  base::RepeatingClosure close_callback_;
  base::RepeatingClosure promote_callback_;
  base::RepeatingClosure split_callback_;
  gfx::Size preferred_card_size_;
  bool reduced_motion_ = false;
  bool show_animation_started_ = false;
  bool dismissing_ = false;
};

}  // namespace ahoi::popup_ui

#endif  // AHOI_BROWSER_UI_POPUP_POPUP_OVERLAY_VIEW_H_
