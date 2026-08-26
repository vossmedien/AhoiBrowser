// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_COOKIE_MANAGER_VIEW_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_COOKIE_MANAGER_VIEW_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_cookie_manager.h"
#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/view.h"

namespace views {
class Checkbox;
class Combobox;
class Label;
class LabelButton;
class ScrollView;
class Textfield;
}  // namespace views

class PrefService;

namespace ahoi {

namespace appearance {
class AppearanceRuntimeSignalSource;
struct GlassPolicy;
}  // namespace appearance

class DeveloperCookieManagerView final : public views::View {
 public:
  DeveloperCookieManagerView(GURL site_url,
                             std::unique_ptr<DeveloperCookieAdapter> adapter,
                             PrefService* prefs = nullptr);
  DeveloperCookieManagerView(const DeveloperCookieManagerView&) = delete;
  DeveloperCookieManagerView& operator=(const DeveloperCookieManagerView&) =
      delete;
  ~DeveloperCookieManagerView() override;

  views::Textfield* initially_focused_view() const { return search_field_; }
  void ReapplyAppearance();

  size_t visible_cookie_count_for_testing() const;
  bool editor_visible_for_testing() const;
  void SetFilterForTesting(std::u16string filter);
  views::LabelButton* delete_visible_button_for_testing() const {
    return delete_visible_button_;
  }
  views::Label* status_label_for_testing() const { return status_label_; }
  const std::optional<std::vector<uint64_t>>& pending_delete_ids_for_testing()
      const {
    return pending_delete_ids_;
  }
  bool busy_for_testing() const { return busy_; }

 private:
  std::unique_ptr<views::View> CreateHeader();
  std::unique_ptr<views::View> CreateEditor();
  std::unique_ptr<views::View> CreateFieldColumn(
      int label_id,
      raw_ptr<views::Textfield>* field);
  std::unique_ptr<views::View> CreateSelectorColumn(
      int label_id,
      std::unique_ptr<views::Combobox> combobox,
      raw_ptr<views::Combobox>* selector);
  void LoadCookies();
  void OnCookiesLoaded(DeveloperCookieLoadResult result);
  void OnFilterChanged();
  void RebuildCookieRows();
  std::vector<uint64_t> VisibleCookieIds() const;
  std::unique_ptr<views::View> CreateCookieRow(const DeveloperCookie& cookie);
  void StartCreate();
  void StartEdit(uint64_t cookie_id);
  void DeleteCookie(uint64_t cookie_id);
  void DeleteVisibleCookies();
  void OnBatchDeleteFinished(DeveloperCookieResult result);
  void ResetBatchDeleteConfirmation();
  void SaveEditor();
  void CancelEditor();
  void SetEditorCookie(const DeveloperCookie* cookie);
  DeveloperCookieDraft EditorDraft() const;
  void OnMutationFinished(int success_string_id, DeveloperCookieResult result);
  void SetBusy(bool busy);
  void ShowStatus(int string_id);
  void ShowError(DeveloperCookieError error);
  const DeveloperCookie* FindCookie(uint64_t cookie_id) const;
  void OnAppearanceChanged(const appearance::GlassPolicy& policy);

  const GURL site_url_;
  const std::unique_ptr<DeveloperCookieAdapter> adapter_;
  std::vector<DeveloperCookie> cookies_;
  std::optional<uint64_t> editing_cookie_id_;
  std::optional<std::vector<uint64_t>> pending_delete_ids_;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::LabelButton> add_button_ = nullptr;
  raw_ptr<views::LabelButton> delete_visible_button_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> rows_container_ = nullptr;
  raw_ptr<views::Label> empty_label_ = nullptr;
  raw_ptr<views::View> editor_ = nullptr;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  raw_ptr<views::Textfield> value_field_ = nullptr;
  raw_ptr<views::Textfield> domain_field_ = nullptr;
  raw_ptr<views::Textfield> path_field_ = nullptr;
  raw_ptr<views::Checkbox> secure_checkbox_ = nullptr;
  raw_ptr<views::Checkbox> http_only_checkbox_ = nullptr;
  raw_ptr<views::Combobox> same_site_combobox_ = nullptr;
  raw_ptr<views::Combobox> expiration_combobox_ = nullptr;
  raw_ptr<views::LabelButton> save_button_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  base::CallbackListSubscription search_subscription_;
  bool busy_ = false;
  std::unique_ptr<appearance::AppearanceRuntimeSignalSource>
      appearance_signal_source_;
  base::WeakPtrFactory<DeveloperCookieManagerView> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_COOKIE_MANAGER_VIEW_H_
