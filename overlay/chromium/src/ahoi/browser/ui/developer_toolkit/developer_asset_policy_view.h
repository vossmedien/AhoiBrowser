// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_ASSET_POLICY_VIEW_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_ASSET_POLICY_VIEW_H_

#include <string>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"
#include "url/gurl.h"

namespace views {
class Checkbox;
class Combobox;
class Textfield;
}  // namespace views

namespace ahoi {

// Compact policy editor shared by CSS and JavaScript assets. It translates
// explicit UI choices into the bounded scope/lifetime model and prevents
// transient or current-tab source from entering sync.
class DeveloperAssetPolicyView final : public views::View {
 public:
  DeveloperAssetPolicyView(const DeveloperAsset& initial_asset,
                           GURL source_url,
                           std::string current_tab_token);
  DeveloperAssetPolicyView(const DeveloperAssetPolicyView&) = delete;
  DeveloperAssetPolicyView& operator=(const DeveloperAssetPolicyView&) = delete;
  ~DeveloperAssetPolicyView() override;

  bool ApplyTo(DeveloperAsset* asset) const;

  void SelectScopeForTesting(DeveloperAssetScopeKind scope);
  void SelectLifetimeForTesting(DeveloperAssetLifetime lifetime);
  void SetScopeValueForTesting(std::u16string value);
  void SetDomainScopeWarningAcceptedForTesting(bool accepted);
  bool domain_scope_warning_visible_for_testing() const;
  void SetSyncEnabledForTesting(bool enabled);
  bool sync_control_enabled_for_testing() const;

 private:
  void OnScopeChanged();
  void OnLifetimeChanged();
  void UpdateSyncAvailability();
  std::string DefaultScopeValue(DeveloperAssetScopeKind scope) const;

  const GURL source_url_;
  const std::string current_tab_token_;
  raw_ptr<views::Combobox> scope_selector_ = nullptr;
  raw_ptr<views::Textfield> scope_value_ = nullptr;
  raw_ptr<views::Checkbox> domain_scope_warning_ = nullptr;
  raw_ptr<views::Combobox> lifetime_selector_ = nullptr;
  raw_ptr<views::Checkbox> sync_enabled_ = nullptr;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_ASSET_POLICY_VIEW_H_
