// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/location_bar_bubble_button.h"

#include <utility>

#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/views/controls/button/image_button_factory.h"

namespace ahoi {

LocationBarBubbleButton::LocationBarBubbleButton(
    views::Button::PressedCallback callback,
    IsSurfaceShowingCallback is_surface_showing_callback)
    : views::ImageButton(std::move(callback)),
      is_surface_showing_callback_(std::move(is_surface_showing_callback)) {
  views::ConfigureVectorImageButton(this);
}

LocationBarBubbleButton::~LocationBarBubbleButton() = default;

void LocationBarBubbleButton::OnEvent(ui::Event* event) {
  // A disabled Views child is still the initial event target, but View's
  // default disabled path returns without marking the event handled. RootView
  // then retries the same press on LocationBarView, whose intended empty-bar
  // behavior forwards it to the omnibox/command bar. Consume only disabled
  // mouse presses here so an unavailable action remains inert instead of
  // activating an unrelated parent surface. Enabled mouse, keyboard, touch,
  // focus and accessibility events keep ImageButton's normal behavior.
  if (event && event->type() == ui::EventType::kMousePressed &&
      !GetEnabledInViewsSubtree()) {
    event->SetHandled();
    return;
  }
  views::ImageButton::OnEvent(event);
}

bool LocationBarBubbleButton::OnMousePressed(const ui::MouseEvent& event) {
  suppress_button_release_ = IsSurfaceShowing();
  return views::ImageButton::OnMousePressed(event);
}

bool LocationBarBubbleButton::IsTriggerableEvent(const ui::Event& event) {
  if (event.IsMouseEvent()) {
    return !IsSurfaceShowing() && !suppress_button_release_;
  }
  return true;
}

bool LocationBarBubbleButton::IsSurfaceShowing() const {
  return !is_surface_showing_callback_.is_null() &&
         is_surface_showing_callback_.Run();
}

BEGIN_METADATA(LocationBarBubbleButton)
END_METADATA

}  // namespace ahoi
