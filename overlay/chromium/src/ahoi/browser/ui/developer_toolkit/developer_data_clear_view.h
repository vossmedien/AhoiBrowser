// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_DATA_CLEAR_VIEW_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_DATA_CLEAR_VIEW_H_

#include <map>
#include <memory>
#include <optional>

#include "ahoi/browser/developer_toolkit/developer_toolkit_types.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Checkbox;
class Combobox;
class Label;
class LabelButton;
}  // namespace views

namespace ahoi {

// Compact, explicit data-clearing surface. Scope, time range and every data
// type are represented in the request instead of being inferred from a broad
// destructive command. Global cleanup always requires a second confirmation.
class DeveloperDataClearView final : public views::View {
  METADATA_HEADER(DeveloperDataClearView, views::View)

 public:
  using ClearCallback =
      base::RepeatingCallback<bool(BrowsingDataClearOptions,
                                   BrowsingDataClearCallback)>;

  explicit DeveloperDataClearView(ClearCallback clear_callback);
  DeveloperDataClearView(const DeveloperDataClearView&) = delete;
  DeveloperDataClearView& operator=(const DeveloperDataClearView&) = delete;
  ~DeveloperDataClearView() override;

  BrowsingDataClearOptions options_for_testing() const;
  const std::optional<BrowsingDataClearOptions>& pending_options_for_testing()
      const {
    return pending_options_;
  }
  views::LabelButton* clear_button_for_testing() const { return clear_button_; }
  views::LabelButton* expand_button_for_testing() const {
    return expand_button_;
  }
  views::Label* status_label_for_testing() const { return status_label_; }
  views::Combobox* scope_combobox_for_testing() const {
    return scope_combobox_;
  }
  views::Combobox* time_range_combobox_for_testing() const {
    return time_range_combobox_;
  }
  views::Checkbox* type_checkbox_for_testing(BrowsingDataType type) const {
    const auto found = type_checkboxes_.find(type);
    return found == type_checkboxes_.end() ? nullptr : found->second;
  }
  bool clear_request_in_flight_for_testing() const {
    return clear_request_in_flight_;
  }
  bool expanded_for_testing() const { return body_->GetVisible(); }

 private:
  std::unique_ptr<views::View> CreateSelectorColumn(
      int label_id,
      std::unique_ptr<views::Combobox> combobox);
  std::unique_ptr<views::View> CreateTypeRow(BrowsingDataType first,
                                             BrowsingDataType second);
  views::Checkbox* AddTypeCheckbox(views::View* parent, BrowsingDataType type);
  void ToggleExpanded();
  void OnScopeChanged();
  void OnSelectionChanged();
  void OnSelectAll();
  void OnClearSelection();
  void OnClearPressed();
  void OnClearFinished(BrowsingDataClearResult result);
  void ResetConfirmation();
  void ShowStatus(int string_id);
  void ShowClearSuccess(const BrowsingDataClearOptions& options);
  void ShowClearPartial(const BrowsingDataClearResult& result);
  void ShowClearFailure(const BrowsingDataClearResult& result);
  std::u16string DataTypeSummary(uint32_t data_type_mask) const;
  uint32_t SelectedDataMask() const;

  const ClearCallback clear_callback_;
  std::map<BrowsingDataType, raw_ptr<views::Checkbox>> type_checkboxes_;
  raw_ptr<views::View> body_ = nullptr;
  raw_ptr<views::LabelButton> expand_button_ = nullptr;
  raw_ptr<views::Combobox> scope_combobox_ = nullptr;
  raw_ptr<views::Combobox> time_range_combobox_ = nullptr;
  raw_ptr<views::LabelButton> clear_button_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  std::optional<BrowsingDataClearOptions> pending_options_;
  bool clear_request_in_flight_ = false;
  base::WeakPtrFactory<DeveloperDataClearView> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_DATA_CLEAR_VIEW_H_
