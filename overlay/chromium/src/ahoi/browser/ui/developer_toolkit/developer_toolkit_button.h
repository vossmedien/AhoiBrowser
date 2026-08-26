// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_BUTTON_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_BUTTON_H_

#include <string>

#include "base/functional/callback.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/label_button.h"

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace ahoi {

// The developer toolkit has one compact button treatment. Keeping the state
// painting here prevents individual actions from silently losing a hover,
// pressed or keyboard-focus affordance as the surface grows.
class DeveloperToolkitButton final : public views::LabelButton {
  METADATA_HEADER(DeveloperToolkitButton, views::LabelButton)

 public:
  DeveloperToolkitButton(PressedCallback callback,
                         std::u16string text,
                         const gfx::VectorIcon* icon = nullptr);
  DeveloperToolkitButton(const DeveloperToolkitButton&) = delete;
  DeveloperToolkitButton& operator=(const DeveloperToolkitButton&) = delete;
  ~DeveloperToolkitButton() override;

  void SetSelected(bool selected);
  bool GetSelected() const { return selected_; }

  // views::View:
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  bool selected_ = false;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_BUTTON_H_
