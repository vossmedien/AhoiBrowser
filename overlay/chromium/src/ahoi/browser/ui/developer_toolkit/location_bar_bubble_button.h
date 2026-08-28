// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_LOCATION_BAR_BUBBLE_BUTTON_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_LOCATION_BAR_BUBBLE_BUTTON_H_

#include "base/functional/callback.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/image_button.h"

namespace ui {
class Event;
class MouseEvent;
}

namespace ahoi {

// An address-bar image button whose action owns a close-on-deactivate surface.
//
// On desktop, clicking an already-open bubble can dismiss it before the button
// receives the matching mouse release. The button explicitly routes a press on
// its own open surface through the controller's close branch and suppresses the
// paired release, so one physical click can never close and immediately reopen
// the surface. Non-mouse activation remains triggerable so keyboard and touch
// use the same controller toggle contract.
class LocationBarBubbleButton final : public views::ImageButton {
  METADATA_HEADER(LocationBarBubbleButton, views::ImageButton)

 public:
  using IsSurfaceShowingCallback = base::RepeatingCallback<bool()>;

  LocationBarBubbleButton(views::Button::PressedCallback callback,
                          IsSurfaceShowingCallback is_surface_showing_callback);
  LocationBarBubbleButton(const LocationBarBubbleButton&) = delete;
  LocationBarBubbleButton& operator=(const LocationBarBubbleButton&) = delete;
  ~LocationBarBubbleButton() override;

  // views::ImageButton:
  void OnEvent(ui::Event* event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  bool IsTriggerableEvent(const ui::Event& event) override;

 private:
  bool IsSurfaceShowing() const;

  IsSurfaceShowingCallback is_surface_showing_callback_;
  bool suppress_button_release_ = false;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_LOCATION_BAR_BUBBLE_BUTTON_H_
