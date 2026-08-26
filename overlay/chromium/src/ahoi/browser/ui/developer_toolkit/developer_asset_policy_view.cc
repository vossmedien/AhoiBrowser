// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_asset_policy_view.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/grit/generated_resources.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "url/origin.h"

namespace ahoi {
namespace {

constexpr std::array<DeveloperAssetScopeKind, 4> kScopes = {
    DeveloperAssetScopeKind::kCurrentTab, DeveloperAssetScopeKind::kOrigin,
    DeveloperAssetScopeKind::kDomain, DeveloperAssetScopeKind::kPath};
constexpr std::array<DeveloperAssetLifetime, 3> kLifetimes = {
    DeveloperAssetLifetime::kOnce, DeveloperAssetLifetime::kReload,
    DeveloperAssetLifetime::kRestart};

template <typename Value, size_t Size>
size_t IndexOf(const std::array<Value, Size>& values, Value selected) {
  const auto found = std::ranges::find(values, selected);
  return found == values.end() ? 0u
                               : static_cast<size_t>(found - values.begin());
}

int ScopeStringId(DeveloperAssetScopeKind scope) {
  switch (scope) {
    case DeveloperAssetScopeKind::kCurrentTab:
      return IDS_AHOI_DEVELOPER_ASSET_SCOPE_TAB;
    case DeveloperAssetScopeKind::kOrigin:
      return IDS_AHOI_DEVELOPER_ASSET_SCOPE_ORIGIN;
    case DeveloperAssetScopeKind::kDomain:
      return IDS_AHOI_DEVELOPER_ASSET_SCOPE_DOMAIN;
    case DeveloperAssetScopeKind::kPath:
      return IDS_AHOI_DEVELOPER_ASSET_SCOPE_PATH;
  }
}

int LifetimeStringId(DeveloperAssetLifetime lifetime) {
  switch (lifetime) {
    case DeveloperAssetLifetime::kOnce:
      return IDS_AHOI_DEVELOPER_ASSET_LIFETIME_ONCE;
    case DeveloperAssetLifetime::kReload:
      return IDS_AHOI_DEVELOPER_ASSET_LIFETIME_RELOAD;
    case DeveloperAssetLifetime::kRestart:
      return IDS_AHOI_DEVELOPER_ASSET_LIFETIME_RESTART;
  }
}

std::unique_ptr<views::Combobox> MakeSelector(std::vector<int> string_ids,
                                              int accessible_name_id) {
  std::vector<ui::SimpleComboboxModel::Item> items;
  for (int string_id : string_ids) {
    items.emplace_back(l10n_util::GetStringUTF16(string_id));
  }
  auto selector = std::make_unique<views::Combobox>(
      std::make_unique<ui::SimpleComboboxModel>(std::move(items)));
  selector->SetAccessibleName(l10n_util::GetStringUTF16(accessible_name_id));
  selector->SetPreferredSize(
      gfx::Size(0, visual_style::kDeveloperToolkitRowHeight));
  selector->SetBackgroundColorId(visual_style::kRaisedSurface);
  selector->SetForegroundColorId(visual_style::kText);
  selector->SetBorderColorId(visual_style::kDivider);
  return selector;
}

std::unique_ptr<views::Label> MakeLabel(int string_id) {
  auto label =
      std::make_unique<views::Label>(l10n_util::GetStringUTF16(string_id));
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(visual_style::kMutedText);
  return label;
}

}  // namespace

DeveloperAssetPolicyView::DeveloperAssetPolicyView(
    const DeveloperAsset& initial_asset,
    GURL source_url,
    std::string current_tab_token)
    : source_url_(std::move(source_url)),
      current_tab_token_(std::move(current_tab_token)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto selectors = std::make_unique<views::View>();
  auto* selector_layout =
      selectors->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  auto scope_column = std::make_unique<views::View>();
  auto* scope_layout =
      scope_column->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  scope_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  scope_column->AddChildView(MakeLabel(IDS_AHOI_DEVELOPER_ASSET_SCOPE));
  std::vector<int> scope_ids;
  for (DeveloperAssetScopeKind scope : kScopes) {
    scope_ids.push_back(ScopeStringId(scope));
  }
  scope_selector_ = scope_column->AddChildView(
      MakeSelector(std::move(scope_ids), IDS_AHOI_DEVELOPER_ASSET_SCOPE));
  views::View* scope_column_ptr =
      selectors->AddChildView(std::move(scope_column));

  auto lifetime_column = std::make_unique<views::View>();
  auto* lifetime_layout =
      lifetime_column->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  lifetime_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  lifetime_column->AddChildView(MakeLabel(IDS_AHOI_DEVELOPER_ASSET_LIFETIME));
  std::vector<int> lifetime_ids;
  for (DeveloperAssetLifetime lifetime : kLifetimes) {
    lifetime_ids.push_back(LifetimeStringId(lifetime));
  }
  lifetime_selector_ = lifetime_column->AddChildView(
      MakeSelector(std::move(lifetime_ids), IDS_AHOI_DEVELOPER_ASSET_LIFETIME));
  views::View* lifetime_column_ptr =
      selectors->AddChildView(std::move(lifetime_column));
  selector_layout->SetFlexForView(scope_column_ptr, 1);
  selector_layout->SetFlexForView(lifetime_column_ptr, 1);
  AddChildView(std::move(selectors));

  scope_value_ = AddChildView(std::make_unique<views::Textfield>());
  scope_value_->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_ASSET_SCOPE_VALUE));
  scope_value_->SetBackgroundColor(visual_style::kRaisedSurface);
  scope_value_->SetTextColorId(visual_style::kText);
  domain_scope_warning_ =
      AddChildView(std::make_unique<views::Checkbox>(l10n_util::GetStringUTF16(
          IDS_AHOI_DEVELOPER_ASSET_SCOPE_DOMAIN_WARNING)));
  domain_scope_warning_->SetTextSubpixelRenderingEnabled(false);
  domain_scope_warning_->SetChecked(
      initial_asset.scope.kind == DeveloperAssetScopeKind::kDomain &&
      initial_asset.domain_scope_warning_accepted);
  sync_enabled_ = AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_SYNC_ASSET)));
  sync_enabled_->SetTextSubpixelRenderingEnabled(false);

  scope_selector_->SetSelectedIndex(IndexOf(kScopes, initial_asset.scope.kind));
  lifetime_selector_->SetSelectedIndex(
      IndexOf(kLifetimes, initial_asset.lifetime));
  sync_enabled_->SetChecked(initial_asset.sync_enabled);
  scope_selector_->SetCallback(base::BindRepeating(
      &DeveloperAssetPolicyView::OnScopeChanged, base::Unretained(this)));
  lifetime_selector_->SetCallback(base::BindRepeating(
      &DeveloperAssetPolicyView::OnLifetimeChanged, base::Unretained(this)));
  OnScopeChanged();
  if (!initial_asset.scope.value.empty()) {
    scope_value_->SetText(base::UTF8ToUTF16(initial_asset.scope.value));
  }
}

DeveloperAssetPolicyView::~DeveloperAssetPolicyView() = default;

bool DeveloperAssetPolicyView::ApplyTo(DeveloperAsset* asset) const {
  if (!asset) {
    return false;
  }
  const size_t scope_index = std::min(
      scope_selector_->GetSelectedIndex().value_or(0u), kScopes.size() - 1);
  const size_t lifetime_index =
      std::min(lifetime_selector_->GetSelectedIndex().value_or(0u),
               kLifetimes.size() - 1);
  DeveloperAssetScope scope{
      .kind = kScopes[scope_index],
      .value = base::UTF16ToUTF8(scope_value_->GetText())};
  if (scope.kind == DeveloperAssetScopeKind::kCurrentTab) {
    scope.value = current_tab_token_;
  } else if (scope.kind == DeveloperAssetScopeKind::kOrigin) {
    scope.value = url::Origin::Create(source_url_).Serialize();
  }
  base::TrimWhitespaceASCII(scope.value, base::TRIM_ALL, &scope.value);
  if (scope.value.empty() ||
      (scope.kind == DeveloperAssetScopeKind::kCurrentTab &&
       scope.value != current_tab_token_)) {
    return false;
  }
  const bool domain_scope = scope.kind == DeveloperAssetScopeKind::kDomain;
  const bool domain_scope_warning_accepted =
      domain_scope && domain_scope_warning_->GetChecked();
  if (domain_scope && asset->enabled && !domain_scope_warning_accepted) {
    return false;
  }
  asset->scope = std::move(scope);
  asset->domain_scope_warning_accepted = domain_scope_warning_accepted;
  asset->lifetime = kLifetimes[lifetime_index];
  asset->sync_enabled = sync_enabled_->GetChecked();
  return true;
}

void DeveloperAssetPolicyView::SelectScopeForTesting(
    DeveloperAssetScopeKind scope) {
  scope_selector_->SetSelectedIndex(IndexOf(kScopes, scope));
  OnScopeChanged();
}

void DeveloperAssetPolicyView::SelectLifetimeForTesting(
    DeveloperAssetLifetime lifetime) {
  lifetime_selector_->SetSelectedIndex(IndexOf(kLifetimes, lifetime));
  OnLifetimeChanged();
}

void DeveloperAssetPolicyView::SetScopeValueForTesting(std::u16string value) {
  scope_value_->SetText(std::move(value));
}

void DeveloperAssetPolicyView::SetDomainScopeWarningAcceptedForTesting(
    bool accepted) {
  domain_scope_warning_->SetChecked(accepted);
}

bool DeveloperAssetPolicyView::domain_scope_warning_visible_for_testing()
    const {
  return domain_scope_warning_->GetVisible();
}

void DeveloperAssetPolicyView::SetSyncEnabledForTesting(bool enabled) {
  sync_enabled_->SetChecked(enabled);
}

bool DeveloperAssetPolicyView::sync_control_enabled_for_testing() const {
  return sync_enabled_->GetEnabled();
}

void DeveloperAssetPolicyView::OnScopeChanged() {
  const DeveloperAssetScopeKind scope = kScopes[std::min(
      scope_selector_->GetSelectedIndex().value_or(0u), kScopes.size() - 1)];
  scope_value_->SetText(base::UTF8ToUTF16(DefaultScopeValue(scope)));
  scope_value_->SetVisible(scope != DeveloperAssetScopeKind::kCurrentTab);
  scope_value_->SetReadOnly(scope == DeveloperAssetScopeKind::kCurrentTab ||
                            scope == DeveloperAssetScopeKind::kOrigin);
  const bool domain_scope = scope == DeveloperAssetScopeKind::kDomain;
  domain_scope_warning_->SetVisible(domain_scope);
  if (!domain_scope) {
    domain_scope_warning_->SetChecked(false);
  }
  if (scope == DeveloperAssetScopeKind::kCurrentTab &&
      kLifetimes[std::min(lifetime_selector_->GetSelectedIndex().value_or(0u),
                          kLifetimes.size() - 1)] ==
          DeveloperAssetLifetime::kRestart) {
    lifetime_selector_->SetSelectedIndex(
        IndexOf(kLifetimes, DeveloperAssetLifetime::kReload));
  }
  UpdateSyncAvailability();
  PreferredSizeChanged();
}

void DeveloperAssetPolicyView::OnLifetimeChanged() {
  const DeveloperAssetScopeKind scope = kScopes[std::min(
      scope_selector_->GetSelectedIndex().value_or(0u), kScopes.size() - 1)];
  const DeveloperAssetLifetime lifetime =
      kLifetimes[std::min(lifetime_selector_->GetSelectedIndex().value_or(0u),
                          kLifetimes.size() - 1)];
  if (scope == DeveloperAssetScopeKind::kCurrentTab &&
      lifetime == DeveloperAssetLifetime::kRestart) {
    lifetime_selector_->SetSelectedIndex(
        IndexOf(kLifetimes, DeveloperAssetLifetime::kReload));
  }
  UpdateSyncAvailability();
}

void DeveloperAssetPolicyView::UpdateSyncAvailability() {
  const DeveloperAssetScopeKind scope = kScopes[std::min(
      scope_selector_->GetSelectedIndex().value_or(0u), kScopes.size() - 1)];
  const DeveloperAssetLifetime lifetime =
      kLifetimes[std::min(lifetime_selector_->GetSelectedIndex().value_or(0u),
                          kLifetimes.size() - 1)];
  const bool can_sync = scope != DeveloperAssetScopeKind::kCurrentTab &&
                        lifetime == DeveloperAssetLifetime::kRestart;
  sync_enabled_->SetEnabled(can_sync);
  if (!can_sync) {
    sync_enabled_->SetChecked(false);
  }
}

std::string DeveloperAssetPolicyView::DefaultScopeValue(
    DeveloperAssetScopeKind scope) const {
  switch (scope) {
    case DeveloperAssetScopeKind::kCurrentTab:
      return std::string();
    case DeveloperAssetScopeKind::kOrigin:
      return url::Origin::Create(source_url_).Serialize();
    case DeveloperAssetScopeKind::kDomain: {
      std::string domain =
          net::registry_controlled_domains::GetDomainAndRegistry(
              source_url_,
              net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
      return domain.empty() ? source_url_.host() : domain;
    }
    case DeveloperAssetScopeKind::kPath:
      return source_url_.path().empty() ? "/" : source_url_.path();
  }
}

}  // namespace ahoi
