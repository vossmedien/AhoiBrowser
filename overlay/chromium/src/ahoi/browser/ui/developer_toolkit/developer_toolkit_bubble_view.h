// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_BUBBLE_VIEW_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_BUBBLE_VIEW_H_

#include <map>
#include <memory>
#include <string>

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_types.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/view.h"

namespace views {
class Checkbox;
class Label;
class LabelButton;
}  // namespace views

class PrefService;

namespace ahoi {

namespace appearance {
class AppearanceRuntimeSignalSource;
struct GlassPolicy;
}  // namespace appearance

class DeveloperToolkitBubbleView final : public views::View {
 public:
  using ExecuteCallback =
      base::RepeatingCallback<DeveloperActionResult(DeveloperAction)>;
  using DataClearCallback =
      base::RepeatingCallback<bool(BrowsingDataClearOptions,
                                   BrowsingDataClearCallback)>;
  using VisibilityCallback =
      base::RepeatingCallback<bool(developer_toolkit_prefs::ToolbarVisibility)>;
  using OpenDevToolsCallback = base::RepeatingClosure;
  using OpenCookieManagerCallback = base::RepeatingClosure;
  using OpenProfileCallback = base::RepeatingClosure;

  DeveloperToolkitBubbleView(
      std::u16string origin_label,
      developer_toolkit_prefs::ToolbarVisibility visibility,
      ExecuteCallback execute_callback,
      DataClearCallback data_clear_callback,
      VisibilityCallback visibility_callback,
      OpenDevToolsCallback open_devtools_callback,
      OpenCookieManagerCallback open_cookie_manager_callback,
      OpenProfileCallback open_profile_callback,
      PrefService* prefs = nullptr,
      DeveloperActivationState initial_activation = {});
  DeveloperToolkitBubbleView(const DeveloperToolkitBubbleView&) = delete;
  DeveloperToolkitBubbleView& operator=(const DeveloperToolkitBubbleView&) =
      delete;
  ~DeveloperToolkitBubbleView() override;

  views::LabelButton* action_button_for_testing(DeveloperAction action) const;
  views::LabelButton* devtools_button_for_testing() const {
    return devtools_button_;
  }
  views::Label* status_label_for_testing() const { return status_label_; }
  views::Label* activation_label_for_testing() const {
    return activation_label_;
  }
  DeveloperActivationState activation_state_for_testing() const {
    return activation_state_;
  }
  void ReapplyAppearance();

 private:
  std::unique_ptr<views::View> CreateActionRow(DeveloperAction first,
                                               DeveloperAction second);
  views::LabelButton* AddActionButton(views::View* parent,
                                      DeveloperAction action);
  void OnActionPressed(DeveloperAction action);
  void UpdateActivationChips();
  void OnToolbarVisibilityChanged();
  void ShowStatus(int string_id);
  void OnAppearanceChanged(const appearance::GlassPolicy& policy);

  const ExecuteCallback execute_callback_;
  const DataClearCallback data_clear_callback_;
  const VisibilityCallback visibility_callback_;
  const OpenDevToolsCallback open_devtools_callback_;
  const OpenCookieManagerCallback open_cookie_manager_callback_;
  const OpenProfileCallback open_profile_callback_;
  std::map<DeveloperAction, raw_ptr<views::LabelButton>> action_buttons_;
  raw_ptr<views::LabelButton> devtools_button_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::Label> activation_label_ = nullptr;
  raw_ptr<views::Checkbox> cookie_visibility_ = nullptr;
  raw_ptr<views::Checkbox> cache_visibility_ = nullptr;
  raw_ptr<views::Checkbox> toolkit_visibility_ = nullptr;
  DeveloperActivationState activation_state_;
  std::unique_ptr<appearance::AppearanceRuntimeSignalSource>
      appearance_signal_source_;
  base::WeakPtrFactory<DeveloperToolkitBubbleView> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_BUBBLE_VIEW_H_
