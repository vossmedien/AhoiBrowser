// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_PRIVACY_PRIVACY_MODE_BUBBLE_VIEW_H_
#define AHOI_BROWSER_UI_PRIVACY_PRIVACY_MODE_BUBBLE_VIEW_H_

#include <optional>
#include <string>

#include "ahoi/browser/privacy/privacy_mode_service.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace views {
class Combobox;
class Label;
class LabelButton;
}  // namespace views

namespace ahoi {

// Compact site panel for Ahoi's two privacy modes. It deliberately exposes a
// three-state origin selector (inherit, strict, compatible), so changing the
// global default never silently destroys a site's explicit exception.
class PrivacyModeBubbleView final : public views::View {
 public:
  using GlobalModeCallback =
      base::RepeatingCallback<bool(privacy::PrivacyMode)>;
  using OriginModeCallback =
      base::RepeatingCallback<bool(std::optional<privacy::PrivacyMode>)>;

  PrivacyModeBubbleView(std::u16string origin_label,
                        privacy::PrivacyMode global_mode,
                        std::optional<privacy::PrivacyMode> origin_mode,
                        bool global_mode_managed,
                        bool site_controls_enabled,
                        bool is_off_the_record,
                        GlobalModeCallback global_mode_callback,
                        OriginModeCallback origin_mode_callback);
  PrivacyModeBubbleView(const PrivacyModeBubbleView&) = delete;
  PrivacyModeBubbleView& operator=(const PrivacyModeBubbleView&) = delete;
  ~PrivacyModeBubbleView() override;

  views::Combobox* global_mode_combobox_for_testing() const {
    return global_mode_combobox_;
  }
  views::Combobox* origin_mode_combobox_for_testing() const {
    return origin_mode_combobox_;
  }
  views::Label* effective_mode_label_for_testing() const {
    return effective_mode_label_;
  }
  views::LabelButton* repair_button_for_testing() const {
    return repair_button_;
  }

 private:
  void OnGlobalModeChanged();
  void OnOriginModeChanged();
  void OnRepairCompatibility();
  void UpdateEffectiveModeLabel();
  void ShowStatus(int string_id);

  const bool site_controls_enabled_;
  const bool is_off_the_record_;
  const GlobalModeCallback global_mode_callback_;
  const OriginModeCallback origin_mode_callback_;
  privacy::PrivacyMode global_mode_;
  std::optional<privacy::PrivacyMode> origin_mode_;
  raw_ptr<views::Combobox> global_mode_combobox_ = nullptr;
  raw_ptr<views::Combobox> origin_mode_combobox_ = nullptr;
  raw_ptr<views::Label> global_mode_description_ = nullptr;
  raw_ptr<views::Label> site_mode_description_ = nullptr;
  raw_ptr<views::Label> effective_mode_label_ = nullptr;
  raw_ptr<views::LabelButton> repair_button_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  bool synchronizing_controls_ = false;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_PRIVACY_PRIVACY_MODE_BUBBLE_VIEW_H_
