// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_RESPONSE_HEADER_ADVANCED_MODE_VIEW_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_RESPONSE_HEADER_ADVANCED_MODE_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace views {
class Checkbox;
class Label;
}  // namespace views

namespace ahoi {

// Device-local consent surface for response-header rules that can weaken CSP
// or CORS. Disabling response overrides clears the acknowledgement so enabling
// them again always requires an explicit local action.
class DeveloperResponseHeaderAdvancedModeView final : public views::View {
  METADATA_HEADER(DeveloperResponseHeaderAdvancedModeView, views::View)

 public:
  DeveloperResponseHeaderAdvancedModeView(bool response_headers_enabled,
                                          bool acknowledged);
  DeveloperResponseHeaderAdvancedModeView(
      const DeveloperResponseHeaderAdvancedModeView&) = delete;
  DeveloperResponseHeaderAdvancedModeView& operator=(
      const DeveloperResponseHeaderAdvancedModeView&) = delete;
  ~DeveloperResponseHeaderAdvancedModeView() override;

  void SetResponseHeadersEnabled(bool enabled);
  bool acknowledged() const;

  views::Checkbox* acknowledgement_for_testing() const {
    return acknowledgement_;
  }
  views::Label* warning_for_testing() const { return warning_; }

 private:
  raw_ptr<views::Label> warning_ = nullptr;
  raw_ptr<views::Checkbox> acknowledgement_ = nullptr;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_RESPONSE_HEADER_ADVANCED_MODE_VIEW_H_
