// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_data_clear_view.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_button.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace ahoi {
namespace {

constexpr std::array<BrowsingDataTimeRange, 5> kTimeRanges = {
    BrowsingDataTimeRange::kLastHour,  BrowsingDataTimeRange::kLast24Hours,
    BrowsingDataTimeRange::kLast7Days, BrowsingDataTimeRange::kLast4Weeks,
    BrowsingDataTimeRange::kAllTime,
};

int TimeRangeStringId(BrowsingDataTimeRange range) {
  switch (range) {
    case BrowsingDataTimeRange::kLastHour:
      return IDS_AHOI_DEVELOPER_DATA_TIME_HOUR;
    case BrowsingDataTimeRange::kLast24Hours:
      return IDS_AHOI_DEVELOPER_DATA_TIME_DAY;
    case BrowsingDataTimeRange::kLast7Days:
      return IDS_AHOI_DEVELOPER_DATA_TIME_WEEK;
    case BrowsingDataTimeRange::kLast4Weeks:
      return IDS_AHOI_DEVELOPER_DATA_TIME_FOUR_WEEKS;
    case BrowsingDataTimeRange::kAllTime:
      return IDS_AHOI_DEVELOPER_DATA_TIME_ALL;
  }
  return IDS_AHOI_DEVELOPER_DATA_TIME_ALL;
}

int TypeStringId(BrowsingDataType type) {
  switch (type) {
    case BrowsingDataType::kCache:
      return IDS_AHOI_DEVELOPER_TOOLBAR_CACHE;
    case BrowsingDataType::kCookies:
      return IDS_AHOI_DEVELOPER_TOOLBAR_COOKIES;
    case BrowsingDataType::kLocalStorage:
      return IDS_AHOI_DEVELOPER_DATA_LOCAL_STORAGE;
    case BrowsingDataType::kSessionStorage:
      return IDS_AHOI_DEVELOPER_DATA_SESSION_STORAGE;
    case BrowsingDataType::kIndexedDb:
      return IDS_AHOI_DEVELOPER_DATA_INDEXED_DB;
    case BrowsingDataType::kCacheStorage:
      return IDS_AHOI_DEVELOPER_DATA_CACHE_STORAGE;
    case BrowsingDataType::kServiceWorkers:
      return IDS_AHOI_DEVELOPER_DATA_SERVICE_WORKERS;
  }
  return IDS_AHOI_DEVELOPER_ACTION_UNAVAILABLE;
}

std::unique_ptr<ui::SimpleComboboxModel> MakeModel(
    std::initializer_list<int> string_ids) {
  std::vector<ui::SimpleComboboxModel::Item> items;
  items.reserve(string_ids.size());
  for (int string_id : string_ids) {
    items.emplace_back(l10n_util::GetStringUTF16(string_id));
  }
  return std::make_unique<ui::SimpleComboboxModel>(std::move(items));
}

std::unique_ptr<views::LabelButton> MakeLinkButton(
    base::RepeatingClosure callback,
    int string_id) {
  auto button = std::make_unique<DeveloperToolkitButton>(
      std::move(callback), l10n_util::GetStringUTF16(string_id));
  button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return button;
}

}  // namespace

BEGIN_METADATA(DeveloperDataClearView)
END_METADATA

DeveloperDataClearView::DeveloperDataClearView(ClearCallback clear_callback)
    : clear_callback_(std::move(clear_callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto expand = std::make_unique<DeveloperToolkitButton>(
      base::BindRepeating(&DeveloperDataClearView::ToggleExpanded,
                          base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_DATA_OPEN));
  expand->SetAccessibleName(std::u16string(expand->GetText()));
  expand_button_ = AddChildView(std::move(expand));

  auto body = std::make_unique<views::View>();
  auto* body_layout = body->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(4, 0, 0, 0),
      4));
  body_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  body_ = AddChildView(std::move(body));
  body_->SetVisible(false);

  auto selectors = std::make_unique<views::View>();
  auto* selectors_layout =
      selectors->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));

  auto scope = std::make_unique<views::Combobox>(
      MakeModel({IDS_AHOI_DEVELOPER_DATA_SCOPE_SITE,
                 IDS_AHOI_DEVELOPER_DATA_SCOPE_GLOBAL}));
  scope->SetSelectedIndex(0);
  scope->SetCallback(base::BindRepeating(
      &DeveloperDataClearView::OnScopeChanged, base::Unretained(this)));
  scope->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_DATA_SCOPE));
  scope->SetBackgroundColorId(visual_style::kRaisedSurface);
  scope->SetForegroundColorId(visual_style::kText);
  scope->SetBorderColorId(visual_style::kDivider);
  scope->SetPreferredSize(
      gfx::Size(0, visual_style::kDeveloperToolkitRowHeight));
  scope_combobox_ = scope.get();
  auto scope_column =
      CreateSelectorColumn(IDS_AHOI_DEVELOPER_DATA_SCOPE, std::move(scope));
  views::View* scope_column_ptr =
      selectors->AddChildView(std::move(scope_column));

  auto time_range = std::make_unique<views::Combobox>(MakeModel(
      {IDS_AHOI_DEVELOPER_DATA_TIME_HOUR, IDS_AHOI_DEVELOPER_DATA_TIME_DAY,
       IDS_AHOI_DEVELOPER_DATA_TIME_WEEK,
       IDS_AHOI_DEVELOPER_DATA_TIME_FOUR_WEEKS,
       IDS_AHOI_DEVELOPER_DATA_TIME_ALL}));
  time_range->SetSelectedIndex(kTimeRanges.size() - 1);
  time_range->SetCallback(base::BindRepeating(
      &DeveloperDataClearView::OnSelectionChanged, base::Unretained(this)));
  time_range->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_DATA_TIME_RANGE));
  time_range->SetBackgroundColorId(visual_style::kRaisedSurface);
  time_range->SetForegroundColorId(visual_style::kText);
  time_range->SetBorderColorId(visual_style::kDivider);
  time_range->SetPreferredSize(
      gfx::Size(0, visual_style::kDeveloperToolkitRowHeight));
  time_range_combobox_ = time_range.get();
  auto time_column = CreateSelectorColumn(IDS_AHOI_DEVELOPER_DATA_TIME_RANGE,
                                          std::move(time_range));
  views::View* time_column_ptr =
      selectors->AddChildView(std::move(time_column));
  selectors_layout->SetFlexForView(scope_column_ptr, 1);
  selectors_layout->SetFlexForView(time_column_ptr, 1);
  body_->AddChildView(std::move(selectors));

  auto network_label = std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_DATA_GROUP_NETWORK),
      views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5_MEDIUM);
  network_label->SetSubpixelRenderingEnabled(false);
  network_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  network_label->SetEnabledColor(visual_style::kMutedText);
  body_->AddChildView(std::move(network_label));
  body_->AddChildView(
      CreateTypeRow(BrowsingDataType::kCache, BrowsingDataType::kCookies));
  auto storage_label = std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_DATA_GROUP_STORAGE),
      views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5_MEDIUM);
  storage_label->SetSubpixelRenderingEnabled(false);
  storage_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  storage_label->SetEnabledColor(visual_style::kMutedText);
  body_->AddChildView(std::move(storage_label));
  body_->AddChildView(CreateTypeRow(BrowsingDataType::kLocalStorage,
                                    BrowsingDataType::kSessionStorage));
  body_->AddChildView(CreateTypeRow(BrowsingDataType::kIndexedDb,
                                    BrowsingDataType::kCacheStorage));
  body_->AddChildView(CreateTypeRow(BrowsingDataType::kServiceWorkers,
                                    BrowsingDataType::kServiceWorkers));
  type_checkboxes_.at(BrowsingDataType::kCache)->SetChecked(true);

  auto selection_actions = std::make_unique<views::View>();
  auto* selection_layout =
      selection_actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  auto* select_all = selection_actions->AddChildView(
      MakeLinkButton(base::BindRepeating(&DeveloperDataClearView::OnSelectAll,
                                         base::Unretained(this)),
                     IDS_AHOI_DEVELOPER_DATA_SELECT_ALL));
  auto* clear_selection = selection_actions->AddChildView(MakeLinkButton(
      base::BindRepeating(&DeveloperDataClearView::OnClearSelection,
                          base::Unretained(this)),
      IDS_AHOI_DEVELOPER_DATA_CLEAR_SELECTION));
  selection_layout->SetFlexForView(select_all, 1);
  selection_layout->SetFlexForView(clear_selection, 1);
  body_->AddChildView(std::move(selection_actions));

  auto clear_button = std::make_unique<DeveloperToolkitButton>(
      base::BindRepeating(&DeveloperDataClearView::OnClearPressed,
                          base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_DATA_CLEAR_SELECTED));
  clear_button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  clear_button->SetAccessibleName(std::u16string(clear_button->GetText()));
  clear_button_ = body_->AddChildView(std::move(clear_button));

  status_label_ = body_->AddChildView(std::make_unique<views::Label>());
  status_label_->SetSubpixelRenderingEnabled(false);
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetMultiLine(true);
  status_label_->SetEnabledColor(visual_style::kMutedText);
  status_label_->SetVisible(false);
}

DeveloperDataClearView::~DeveloperDataClearView() = default;

void DeveloperDataClearView::ToggleExpanded() {
  const bool expanded = !body_->GetVisible();
  body_->SetVisible(expanded);
  expand_button_->SetText(l10n_util::GetStringUTF16(
      expanded ? IDS_AHOI_DEVELOPER_DATA_CLOSE : IDS_AHOI_DEVELOPER_DATA_OPEN));
  expand_button_->SetAccessibleName(std::u16string(expand_button_->GetText()));
  PreferredSizeChanged();
}

BrowsingDataClearOptions DeveloperDataClearView::options_for_testing() const {
  const size_t scope_index = scope_combobox_->GetSelectedIndex().value_or(0);
  const size_t time_index = std::min(
      time_range_combobox_->GetSelectedIndex().value_or(kTimeRanges.size() - 1),
      kTimeRanges.size() - 1);
  return BrowsingDataClearOptions{
      .target = scope_index == 0 ? BrowsingDataTarget::kCurrentSite
                                 : BrowsingDataTarget::kGlobal,
      .time_range = kTimeRanges[time_index],
      .data_type_mask = SelectedDataMask(),
  };
}

std::unique_ptr<views::View> DeveloperDataClearView::CreateSelectorColumn(
    int label_id,
    std::unique_ptr<views::Combobox> combobox) {
  auto column = std::make_unique<views::View>();
  auto* layout = column->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* label = column->AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(label_id), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5_MEDIUM));
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(visual_style::kMutedText);
  column->AddChildView(std::move(combobox));
  return column;
}

std::unique_ptr<views::View> DeveloperDataClearView::CreateTypeRow(
    BrowsingDataType first,
    BrowsingDataType second) {
  auto row = std::make_unique<views::View>();
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  views::Checkbox* first_checkbox = AddTypeCheckbox(row.get(), first);
  layout->SetFlexForView(first_checkbox, 1);
  if (second != first) {
    views::Checkbox* second_checkbox = AddTypeCheckbox(row.get(), second);
    layout->SetFlexForView(second_checkbox, 1);
  } else {
    auto* spacer = row->AddChildView(std::make_unique<views::View>());
    layout->SetFlexForView(spacer, 1);
  }
  return row;
}

views::Checkbox* DeveloperDataClearView::AddTypeCheckbox(
    views::View* parent,
    BrowsingDataType type) {
  auto checkbox = std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(TypeStringId(type)),
      base::BindRepeating(&DeveloperDataClearView::OnSelectionChanged,
                          base::Unretained(this)));
  checkbox->SetTextSubpixelRenderingEnabled(false);
  checkbox->SetFocusBehavior(FocusBehavior::ALWAYS);
  views::Checkbox* const result = parent->AddChildView(std::move(checkbox));
  type_checkboxes_.emplace(type, result);
  return result;
}

void DeveloperDataClearView::OnScopeChanged() {
  ResetConfirmation();
  const bool global = scope_combobox_->GetSelectedIndex().value_or(0) != 0;
  views::Checkbox* const session =
      type_checkboxes_.at(BrowsingDataType::kSessionStorage);
  session->SetEnabled(!global);
  if (global && session->GetChecked()) {
    session->SetChecked(false);
    ShowStatus(IDS_AHOI_DEVELOPER_DATA_SESSION_SITE_ONLY);
  }
}

void DeveloperDataClearView::OnSelectionChanged() {
  ResetConfirmation();
}

void DeveloperDataClearView::OnSelectAll() {
  ResetConfirmation();
  for (const auto& entry : type_checkboxes_) {
    if (entry.second->GetEnabled()) {
      entry.second->SetChecked(true);
    }
  }
}

void DeveloperDataClearView::OnClearSelection() {
  ResetConfirmation();
  for (const auto& entry : type_checkboxes_) {
    entry.second->SetChecked(false);
  }
}

void DeveloperDataClearView::OnClearPressed() {
  if (clear_request_in_flight_) {
    return;
  }

  const BrowsingDataClearOptions options = options_for_testing();
  if (options.data_type_mask == 0) {
    ResetConfirmation();
    ShowStatus(IDS_AHOI_DEVELOPER_DATA_EMPTY);
    return;
  }

  const uint32_t non_cache_types =
      options.data_type_mask & ~ToMask(BrowsingDataType::kCache);
  const bool needs_confirmation =
      options.target == BrowsingDataTarget::kGlobal || non_cache_types != 0;

  // The first destructive click stores a value copy, not a reference to live
  // controls. Every subsequent step uses that immutable snapshot. A changed
  // selector clears it through OnSelectionChanged/OnScopeChanged and must
  // start confirmation again.
  if (pending_options_ && *pending_options_ != options) {
    ResetConfirmation();
  }

  if (needs_confirmation && !pending_options_) {
    pending_options_ = options;
    const bool global = options.target == BrowsingDataTarget::kGlobal;
    clear_button_->SetText(l10n_util::GetStringUTF16(
        global ? IDS_AHOI_DEVELOPER_DATA_CONFIRM_GLOBAL
               : IDS_AHOI_DEVELOPER_DATA_CONFIRM_SITE));
    ShowStatus(global ? IDS_AHOI_DEVELOPER_DATA_CONFIRM_HINT_GLOBAL
                      : IDS_AHOI_DEVELOPER_DATA_CONFIRM_HINT_SITE);
    return;
  }

  const BrowsingDataClearOptions request = pending_options_.value_or(options);
  // Consume the pending snapshot before entering the callback. This makes a
  // re-entrant click (or a callback that pumps a nested UI loop) harmless and
  // guarantees one callback for one confirmed request.
  ResetConfirmation();
  clear_request_in_flight_ = true;
  clear_button_->SetEnabled(false);
  const bool accepted = clear_callback_.Run(
      request, base::BindOnce(&DeveloperDataClearView::OnClearFinished,
                              weak_ptr_factory_.GetWeakPtr()));
  if (!accepted && clear_request_in_flight_) {
    clear_request_in_flight_ = false;
    clear_button_->SetEnabled(true);
    ShowStatus(IDS_AHOI_DEVELOPER_ACTION_UNAVAILABLE);
  }
}

void DeveloperDataClearView::OnClearFinished(BrowsingDataClearResult result) {
  if (!clear_request_in_flight_) {
    return;
  }
  clear_request_in_flight_ = false;
  clear_button_->SetEnabled(true);
  switch (result.status()) {
    case BrowsingDataClearStatus::kSucceeded:
      ShowClearSuccess(result.options);
      return;
    case BrowsingDataClearStatus::kPartiallySucceeded:
      ShowClearPartial(result);
      return;
    case BrowsingDataClearStatus::kFailed:
      ShowClearFailure(result);
      return;
  }
}

void DeveloperDataClearView::ResetConfirmation() {
  if (!pending_options_) {
    return;
  }
  pending_options_.reset();
  clear_button_->SetText(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_DATA_CLEAR_SELECTED));
  // The confirmation hint describes the discarded snapshot and must not stay
  // visible after the user changes scope, time or data types.
  status_label_->SetVisible(false);
  PreferredSizeChanged();
}

void DeveloperDataClearView::ShowStatus(int string_id) {
  status_label_->SetText(l10n_util::GetStringUTF16(string_id));
  status_label_->SetVisible(true);
  PreferredSizeChanged();
}

void DeveloperDataClearView::ShowClearSuccess(
    const BrowsingDataClearOptions& options) {
  const std::u16string data_type_summary =
      DataTypeSummary(options.data_type_mask);
  const int success_string_id = options.target == BrowsingDataTarget::kGlobal
                                    ? IDS_AHOI_DEVELOPER_DATA_DONE_GLOBAL
                                    : IDS_AHOI_DEVELOPER_DATA_DONE_SITE;
  const std::u16string success_format =
      l10n_util::GetStringUTF16(success_string_id);
  if (success_format.empty()) {
    // A minimal Views-only test bundle does not contain Chrome's generated
    // locale pack. Keep the state transition observable without asking GRIT's
    // formatter to substitute into a missing format string.
    status_label_->SetText(u"");
  } else {
    status_label_->SetText(l10n_util::GetStringFUTF16(
        success_string_id, data_type_summary,
        l10n_util::GetStringUTF16(TimeRangeStringId(options.time_range))));
  }
  status_label_->SetVisible(true);
  PreferredSizeChanged();
}

void DeveloperDataClearView::ShowClearPartial(
    const BrowsingDataClearResult& result) {
  const int string_id = result.options.target == BrowsingDataTarget::kGlobal
                            ? IDS_AHOI_DEVELOPER_DATA_PARTIAL_GLOBAL
                            : IDS_AHOI_DEVELOPER_DATA_PARTIAL_SITE;
  if (l10n_util::GetStringUTF16(string_id).empty()) {
    status_label_->SetText(u"");
  } else {
    status_label_->SetText(l10n_util::GetStringFUTF16(
        string_id, DataTypeSummary(result.completed_data_type_mask()),
        DataTypeSummary(result.failed_data_type_mask),
        l10n_util::GetStringUTF16(
            TimeRangeStringId(result.options.time_range))));
  }
  status_label_->SetVisible(true);
  PreferredSizeChanged();
}

void DeveloperDataClearView::ShowClearFailure(
    const BrowsingDataClearResult& result) {
  const int string_id = result.options.target == BrowsingDataTarget::kGlobal
                            ? IDS_AHOI_DEVELOPER_DATA_FAILED_GLOBAL
                            : IDS_AHOI_DEVELOPER_DATA_FAILED_SITE;
  if (l10n_util::GetStringUTF16(string_id).empty()) {
    status_label_->SetText(u"");
  } else {
    status_label_->SetText(l10n_util::GetStringFUTF16(
        string_id, DataTypeSummary(result.failed_data_type_mask),
        l10n_util::GetStringUTF16(
            TimeRangeStringId(result.options.time_range))));
  }
  status_label_->SetVisible(true);
  PreferredSizeChanged();
}

std::u16string DeveloperDataClearView::DataTypeSummary(
    uint32_t data_type_mask) const {
  std::vector<std::u16string> data_types;
  for (const auto& entry : type_checkboxes_) {
    const BrowsingDataType type = entry.first;
    if ((data_type_mask & ToMask(type)) != 0) {
      data_types.push_back(l10n_util::GetStringUTF16(TypeStringId(type)));
    }
  }
  return base::JoinString(data_types, u", ");
}

uint32_t DeveloperDataClearView::SelectedDataMask() const {
  uint32_t mask = 0;
  for (const auto& [type, checkbox] : type_checkboxes_) {
    if (checkbox->GetEnabled() && checkbox->GetChecked()) {
      mask |= ToMask(type);
    }
  }
  return mask;
}

}  // namespace ahoi
