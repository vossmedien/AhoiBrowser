// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_cookie_manager_view.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"
#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_button.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/grit/generated_resources.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/client_view.h"

namespace ahoi {
namespace {

constexpr std::array<DeveloperCookieSameSite, 4> kSameSiteValues = {
    DeveloperCookieSameSite::kUnspecified, DeveloperCookieSameSite::kNone,
    DeveloperCookieSameSite::kLax, DeveloperCookieSameSite::kStrict};
constexpr std::array<DeveloperCookieExpiration, 6> kExpirationValues = {
    DeveloperCookieExpiration::kKeep,
    DeveloperCookieExpiration::kSession,
    DeveloperCookieExpiration::kOneDay,
    DeveloperCookieExpiration::kSevenDays,
    DeveloperCookieExpiration::kThirtyDays,
    DeveloperCookieExpiration::kOneYear};

std::unique_ptr<ui::SimpleComboboxModel> MakeModel(
    std::initializer_list<int> string_ids) {
  std::vector<ui::SimpleComboboxModel::Item> items;
  for (int string_id : string_ids) {
    items.emplace_back(l10n_util::GetStringUTF16(string_id));
  }
  return std::make_unique<ui::SimpleComboboxModel>(std::move(items));
}

std::unique_ptr<views::Label> MakeLabel(
    std::u16string text,
    int style = views::style::STYLE_BODY_4) {
  auto label = std::make_unique<views::Label>(
      std::move(text), views::style::CONTEXT_LABEL, style);
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

std::unique_ptr<views::MdTextButton> MakeSmallButton(
    views::Button::PressedCallback callback,
    int string_id) {
  auto button = std::make_unique<views::MdTextButton>(
      std::move(callback), l10n_util::GetStringUTF16(string_id));
  button->SetCustomPadding(gfx::Insets::VH(4, 8));
  button->SetCornerRadius(visual_style::kRowCornerRadius);
  button->SetTextSubpixelRenderingEnabled(false);
  return button;
}

int ErrorStringId(DeveloperCookieError error) {
  switch (error) {
    case DeveloperCookieError::kInvalidName:
      return IDS_AHOI_DEVELOPER_COOKIE_ERROR_NAME;
    case DeveloperCookieError::kInvalidValue:
      return IDS_AHOI_DEVELOPER_COOKIE_ERROR_VALUE;
    case DeveloperCookieError::kInvalidDomain:
      return IDS_AHOI_DEVELOPER_COOKIE_ERROR_DOMAIN;
    case DeveloperCookieError::kInvalidPath:
      return IDS_AHOI_DEVELOPER_COOKIE_ERROR_PATH;
    case DeveloperCookieError::kInvalidSameSite:
      return IDS_AHOI_DEVELOPER_COOKIE_ERROR_SAMESITE;
    case DeveloperCookieError::kInvalidPartitioned:
      return IDS_AHOI_DEVELOPER_COOKIE_ERROR_PARTITIONED;
    case DeveloperCookieError::kInvalidPrefix:
      return IDS_AHOI_DEVELOPER_COOKIE_ERROR_PREFIX;
    case DeveloperCookieError::kInvalidExpiration:
      return IDS_AHOI_DEVELOPER_COOKIE_ERROR_EXPIRATION;
    case DeveloperCookieError::kPartiallySucceeded:
      return IDS_AHOI_DEVELOPER_COOKIE_PARTIAL;
    case DeveloperCookieError::kNone:
      return IDS_AHOI_DEVELOPER_COOKIE_SAVED;
    case DeveloperCookieError::kUnsupportedTarget:
    case DeveloperCookieError::kNotFound:
    case DeveloperCookieError::kRejected:
    case DeveloperCookieError::kUnavailable:
      return IDS_AHOI_DEVELOPER_COOKIE_ERROR_GENERIC;
  }
}

}  // namespace

DeveloperCookieManagerView::DeveloperCookieManagerView(
    GURL site_url,
    std::unique_ptr<DeveloperCookieAdapter> adapter,
    PrefService* prefs)
    : site_url_(std::move(site_url)), adapter_(std::move(adapter)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_MANAGER_TITLE));

  AddChildView(CreateHeader());

  auto search = std::make_unique<views::Textfield>();
  search->SetPlaceholderText(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_SEARCH_PLACEHOLDER));
  search->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_SEARCH_PLACEHOLDER));
  search->SetBackgroundColor(visual_style::kRaisedSurface);
  search->SetTextColorId(visual_style::kText);
  search_field_ = AddChildView(std::move(search));
  search_subscription_ = search_field_->AddTextChangedCallback(
      base::BindRepeating(&DeveloperCookieManagerView::OnFilterChanged,
                          base::Unretained(this)));

  auto batch_actions = std::make_unique<views::View>();
  auto* batch_layout =
      batch_actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  auto delete_visible = MakeSmallButton(
      base::BindRepeating(&DeveloperCookieManagerView::DeleteVisibleCookies,
                          base::Unretained(this)),
      IDS_AHOI_DEVELOPER_COOKIE_DELETE_VISIBLE);
  delete_visible_button_ =
      batch_actions->AddChildView(std::move(delete_visible));
  batch_layout->SetFlexForView(delete_visible_button_, 1);
  AddChildView(std::move(batch_actions));

  auto scroll = std::make_unique<views::ScrollView>();
  scroll->SetBackgroundColor(std::nullopt);
  scroll->ClipHeightTo(84, visual_style::kDeveloperCookieListMaximumHeight);
  auto rows = std::make_unique<views::View>();
  auto* rows_layout = rows->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  rows_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  rows_container_ = rows.get();
  scroll->SetContents(std::move(rows));
  scroll_view_ = AddChildView(std::move(scroll));

  empty_label_ = AddChildView(
      MakeLabel(l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_LOADING)));
  empty_label_->SetEnabledColor(visual_style::kMutedText);

  editor_ = AddChildView(CreateEditor());
  editor_->SetVisible(false);

  status_label_ = AddChildView(std::make_unique<views::Label>());
  status_label_->SetSubpixelRenderingEnabled(false);
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetMultiLine(true);
  status_label_->SetEnabledColor(visual_style::kMutedText);
  status_label_->SetVisible(false);

  appearance_signal_source_ =
      std::make_unique<appearance::AppearanceRuntimeSignalSource>(
          prefs,
          base::BindRepeating(&DeveloperCookieManagerView::OnAppearanceChanged,
                              weak_ptr_factory_.GetWeakPtr()));
  OnAppearanceChanged(appearance_signal_source_->policy());
  LoadCookies();
}

DeveloperCookieManagerView::~DeveloperCookieManagerView() = default;

void DeveloperCookieManagerView::ReapplyAppearance() {
  OnAppearanceChanged(appearance_signal_source_->policy());
}

size_t DeveloperCookieManagerView::visible_cookie_count_for_testing() const {
  size_t count = 0;
  for (const DeveloperCookie& cookie : cookies_) {
    if (DeveloperCookieMatchesFilter(cookie, search_field_->GetText())) {
      ++count;
    }
  }
  return count;
}

bool DeveloperCookieManagerView::editor_visible_for_testing() const {
  return editor_->GetVisible();
}

void DeveloperCookieManagerView::SetFilterForTesting(std::u16string filter) {
  search_field_->SetText(std::move(filter));
  OnFilterChanged();
}

std::unique_ptr<views::View> DeveloperCookieManagerView::CreateHeader() {
  auto header = std::make_unique<views::View>();
  auto* layout = header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));

  auto title_column = std::make_unique<views::View>();
  auto* title_layout =
      title_column->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 1));
  title_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* title = title_column->AddChildView(MakeLabel(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_MANAGER_TITLE),
      views::style::STYLE_HEADLINE_5));
  title->SetEnabledColor(visual_style::kText);
  auto* origin = title_column->AddChildView(
      MakeLabel(base::UTF8ToUTF16(site_url_.host())));
  origin->SetEnabledColor(visual_style::kMutedText);
  views::View* title_ptr = header->AddChildView(std::move(title_column));
  layout->SetFlexForView(title_ptr, 1);

  auto add = MakeSmallButton(
      base::BindRepeating(&DeveloperCookieManagerView::StartCreate,
                          base::Unretained(this)),
      IDS_AHOI_DEVELOPER_COOKIE_NEW);
  add->SetStyle(ui::ButtonStyle::kProminent);
  add_button_ = header->AddChildView(std::move(add));
  return header;
}

std::unique_ptr<views::View> DeveloperCookieManagerView::CreateEditor() {
  auto editor = std::make_unique<views::View>();
  auto* layout = editor->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(8, 0), 6));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  editor->AddChildView(std::make_unique<views::Separator>());

  const auto add_field_row =
      [this, &editor](int first_label, raw_ptr<views::Textfield>* first,
                      int second_label, raw_ptr<views::Textfield>* second) {
        auto row = std::make_unique<views::View>();
        auto* row_layout =
            row->SetLayoutManager(std::make_unique<views::BoxLayout>(
                views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
        views::View* first_column =
            row->AddChildView(CreateFieldColumn(first_label, first));
        views::View* second_column =
            row->AddChildView(CreateFieldColumn(second_label, second));
        row_layout->SetFlexForView(first_column, 1);
        row_layout->SetFlexForView(second_column, 1);
        editor->AddChildView(std::move(row));
      };
  add_field_row(IDS_AHOI_DEVELOPER_COOKIE_NAME, &name_field_,
                IDS_AHOI_DEVELOPER_COOKIE_DOMAIN, &domain_field_);
  add_field_row(IDS_AHOI_DEVELOPER_COOKIE_VALUE, &value_field_,
                IDS_AHOI_DEVELOPER_COOKIE_PATH, &path_field_);

  auto selectors = std::make_unique<views::View>();
  auto* selectors_layout =
      selectors->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  views::View* same_site_column = selectors->AddChildView(CreateSelectorColumn(
      IDS_AHOI_DEVELOPER_COOKIE_SAMESITE,
      std::make_unique<views::Combobox>(
          MakeModel({IDS_AHOI_DEVELOPER_COOKIE_SAMESITE_UNSPECIFIED,
                     IDS_AHOI_DEVELOPER_COOKIE_SAMESITE_NONE,
                     IDS_AHOI_DEVELOPER_COOKIE_SAMESITE_LAX,
                     IDS_AHOI_DEVELOPER_COOKIE_SAMESITE_STRICT})),
      &same_site_combobox_));
  same_site_combobox_->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_SAMESITE));
  same_site_combobox_->SetSelectedIndex(0);
  views::View* expiration_column = selectors->AddChildView(CreateSelectorColumn(
      IDS_AHOI_DEVELOPER_COOKIE_EXPIRATION,
      std::make_unique<views::Combobox>(
          MakeModel({IDS_AHOI_DEVELOPER_COOKIE_EXPIRATION_KEEP,
                     IDS_AHOI_DEVELOPER_COOKIE_EXPIRATION_SESSION,
                     IDS_AHOI_DEVELOPER_COOKIE_EXPIRATION_DAY,
                     IDS_AHOI_DEVELOPER_COOKIE_EXPIRATION_WEEK,
                     IDS_AHOI_DEVELOPER_COOKIE_EXPIRATION_MONTH,
                     IDS_AHOI_DEVELOPER_COOKIE_EXPIRATION_YEAR})),
      &expiration_combobox_));
  expiration_combobox_->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_EXPIRATION));
  expiration_combobox_->SetSelectedIndex(1);
  selectors_layout->SetFlexForView(same_site_column, 1);
  selectors_layout->SetFlexForView(expiration_column, 1);
  editor->AddChildView(std::move(selectors));

  auto flags = std::make_unique<views::View>();
  auto* flags_layout =
      flags->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 14));
  secure_checkbox_ = flags->AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_SECURE)));
  http_only_checkbox_ = flags->AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_HTTP_ONLY)));
  partitioned_checkbox_ = flags->AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_PARTITIONED)));
  secure_checkbox_->SetTextSubpixelRenderingEnabled(false);
  http_only_checkbox_->SetTextSubpixelRenderingEnabled(false);
  partitioned_checkbox_->SetTextSubpixelRenderingEnabled(false);
  flags_layout->SetFlexForView(secure_checkbox_, 1);
  flags_layout->SetFlexForView(http_only_checkbox_, 1);
  flags_layout->SetFlexForView(partitioned_checkbox_, 1);
  editor->AddChildView(std::move(flags));

  auto actions = std::make_unique<views::View>();
  auto* action_layout =
      actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  auto* cancel = actions->AddChildView(MakeSmallButton(
      base::BindRepeating(&DeveloperCookieManagerView::CancelEditor,
                          base::Unretained(this)),
      IDS_AHOI_DEVELOPER_PROFILE_CANCEL));
  auto save = MakeSmallButton(
      base::BindRepeating(&DeveloperCookieManagerView::SaveEditor,
                          base::Unretained(this)),
      IDS_AHOI_DEVELOPER_COOKIE_SAVE);
  save->SetStyle(ui::ButtonStyle::kProminent);
  save_button_ = actions->AddChildView(std::move(save));
  action_layout->SetFlexForView(cancel, 1);
  action_layout->SetFlexForView(save_button_, 1);
  editor->AddChildView(std::move(actions));
  return editor;
}

std::unique_ptr<views::View> DeveloperCookieManagerView::CreateFieldColumn(
    int label_id,
    raw_ptr<views::Textfield>* field) {
  auto column = std::make_unique<views::View>();
  auto* layout = column->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* label = column->AddChildView(MakeLabel(
      l10n_util::GetStringUTF16(label_id), views::style::STYLE_BODY_5_MEDIUM));
  label->SetEnabledColor(visual_style::kMutedText);
  auto textfield = std::make_unique<views::Textfield>();
  textfield->SetAccessibleName(l10n_util::GetStringUTF16(label_id));
  textfield->SetBackgroundColor(visual_style::kRaisedSurface);
  textfield->SetTextColorId(visual_style::kText);
  *field = column->AddChildView(std::move(textfield));
  return column;
}

std::unique_ptr<views::View> DeveloperCookieManagerView::CreateSelectorColumn(
    int label_id,
    std::unique_ptr<views::Combobox> combobox,
    raw_ptr<views::Combobox>* selector) {
  auto column = std::make_unique<views::View>();
  auto* layout = column->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* label = column->AddChildView(MakeLabel(
      l10n_util::GetStringUTF16(label_id), views::style::STYLE_BODY_5_MEDIUM));
  label->SetEnabledColor(visual_style::kMutedText);
  *selector = column->AddChildView(std::move(combobox));
  return column;
}

void DeveloperCookieManagerView::LoadCookies() {
  ResetBatchDeleteConfirmation();
  SetBusy(true);
  empty_label_->SetText(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_LOADING));
  empty_label_->SetVisible(true);
  if (!adapter_ ||
      !adapter_->Load(
          site_url_,
          base::BindOnce(&DeveloperCookieManagerView::OnCookiesLoaded,
                         weak_ptr_factory_.GetWeakPtr()))) {
    SetBusy(false);
    ShowError(DeveloperCookieError::kUnavailable);
  }
}

void DeveloperCookieManagerView::OnCookiesLoaded(
    DeveloperCookieLoadResult result) {
  SetBusy(false);
  if (!result.succeeded()) {
    ShowError(result.error);
    return;
  }
  ResetBatchDeleteConfirmation();
  cookies_ = std::move(result.cookies);
  RebuildCookieRows();
}

void DeveloperCookieManagerView::OnFilterChanged() {
  ResetBatchDeleteConfirmation();
  RebuildCookieRows();
}

void DeveloperCookieManagerView::RebuildCookieRows() {
  if (!rows_container_) {
    return;
  }
  rows_container_->RemoveAllChildViews();
  size_t visible_count = 0;
  for (const DeveloperCookie& cookie : cookies_) {
    if (!DeveloperCookieMatchesFilter(cookie, search_field_->GetText())) {
      continue;
    }
    rows_container_->AddChildView(CreateCookieRow(cookie));
    ++visible_count;
  }
  empty_label_->SetText(l10n_util::GetStringUTF16(
      cookies_.empty() ? IDS_AHOI_DEVELOPER_COOKIE_EMPTY
                       : IDS_AHOI_DEVELOPER_COOKIE_NO_MATCHES));
  empty_label_->SetVisible(!busy_ && visible_count == 0);
  scroll_view_->SetVisible(visible_count != 0);
  delete_visible_button_->SetEnabled(!busy_ && visible_count != 0);
  rows_container_->InvalidateLayout();
  scroll_view_->InvalidateLayout();
  PreferredSizeChanged();
}

std::vector<uint64_t> DeveloperCookieManagerView::VisibleCookieIds() const {
  std::vector<uint64_t> ids;
  for (const DeveloperCookie& cookie : cookies_) {
    if (DeveloperCookieMatchesFilter(cookie, search_field_->GetText())) {
      ids.push_back(cookie.id);
    }
  }
  return ids;
}

std::unique_ptr<views::View> DeveloperCookieManagerView::CreateCookieRow(
    const DeveloperCookie& cookie) {
  auto row = std::make_unique<views::View>();
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(5, 7), 6));

  auto text = std::make_unique<views::View>();
  auto* text_layout = text->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* name = text->AddChildView(MakeLabel(base::UTF8ToUTF16(cookie.name),
                                            views::style::STYLE_BODY_4_MEDIUM));
  name->SetEnabledColor(visual_style::kText);
  name->SetElideBehavior(gfx::ELIDE_TAIL);
  std::u16string scope = base::UTF8ToUTF16(cookie.domain + cookie.path);
  if (cookie.partitioned) {
    scope.append(u"  [");
    scope.append(
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_PARTITIONED));
    scope.push_back(u']');
  }
  auto* domain = text->AddChildView(MakeLabel(scope));
  domain->SetEnabledColor(visual_style::kMutedText);
  domain->SetElideBehavior(gfx::ELIDE_MIDDLE);
  auto* value = text->AddChildView(MakeLabel(base::UTF8ToUTF16(cookie.value)));
  value->SetEnabledColor(visual_style::kMutedText);
  value->SetElideBehavior(gfx::ELIDE_TAIL);
  views::View* text_ptr = row->AddChildView(std::move(text));
  layout->SetFlexForView(text_ptr, 1);

  row->AddChildView(MakeSmallButton(
      base::BindRepeating(&DeveloperCookieManagerView::StartEdit,
                          base::Unretained(this), cookie.id),
      IDS_AHOI_DEVELOPER_COOKIE_EDIT));
  row->AddChildView(MakeSmallButton(
      base::BindRepeating(&DeveloperCookieManagerView::DeleteCookie,
                          base::Unretained(this), cookie.id),
      IDS_AHOI_DEVELOPER_COOKIE_DELETE));
  row->SetBackground(views::CreateRoundedRectBackground(
      visual_style::kRaisedSurface, visual_style::kRowCornerRadius));
  return row;
}

void DeveloperCookieManagerView::StartCreate() {
  if (busy_) {
    return;
  }
  ResetBatchDeleteConfirmation();
  editing_cookie_id_.reset();
  SetEditorCookie(nullptr);
}

void DeveloperCookieManagerView::StartEdit(uint64_t cookie_id) {
  if (busy_) {
    return;
  }
  ResetBatchDeleteConfirmation();
  const DeveloperCookie* cookie = FindCookie(cookie_id);
  if (!cookie) {
    ShowError(DeveloperCookieError::kNotFound);
    return;
  }
  editing_cookie_id_ = cookie_id;
  SetEditorCookie(cookie);
}

void DeveloperCookieManagerView::DeleteCookie(uint64_t cookie_id) {
  if (busy_ || !adapter_) {
    return;
  }
  ResetBatchDeleteConfirmation();
  SetBusy(true);
  if (!adapter_->Delete(
          site_url_, cookie_id,
          base::BindOnce(&DeveloperCookieManagerView::OnMutationFinished,
                         weak_ptr_factory_.GetWeakPtr(),
                         IDS_AHOI_DEVELOPER_COOKIE_DELETED))) {
    SetBusy(false);
    ShowError(DeveloperCookieError::kUnavailable);
  }
}

void DeveloperCookieManagerView::DeleteVisibleCookies() {
  if (busy_ || !adapter_) {
    return;
  }

  std::vector<uint64_t> visible_ids = VisibleCookieIds();
  if (visible_ids.empty()) {
    ResetBatchDeleteConfirmation();
    return;
  }
  if (!pending_delete_ids_ || *pending_delete_ids_ != visible_ids) {
    pending_delete_ids_ = std::move(visible_ids);
    delete_visible_button_->SetText(l10n_util::GetStringUTF16(
        IDS_AHOI_DEVELOPER_COOKIE_CONFIRM_DELETE_VISIBLE));
    ShowStatus(IDS_AHOI_DEVELOPER_COOKIE_CONFIRM_DELETE_HINT);
    return;
  }

  // Consume the immutable filter snapshot before entering an adapter callback.
  // This prevents a synchronous validation failure or nested UI loop from
  // dispatching the same destructive request twice.
  std::vector<uint64_t> request = std::move(*pending_delete_ids_);
  pending_delete_ids_.reset();
  delete_visible_button_->SetText(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_DELETE_VISIBLE));
  SetBusy(true);
  if (!adapter_->DeleteMany(
          site_url_, std::move(request),
          base::BindOnce(&DeveloperCookieManagerView::OnBatchDeleteFinished,
                         weak_ptr_factory_.GetWeakPtr()))) {
    SetBusy(false);
    ShowError(DeveloperCookieError::kUnavailable);
  }
}

void DeveloperCookieManagerView::OnBatchDeleteFinished(
    DeveloperCookieResult result) {
  SetBusy(false);
  if (result.succeeded()) {
    ShowStatus(IDS_AHOI_DEVELOPER_COOKIE_DELETED_MANY);
  } else if (result.error == DeveloperCookieError::kPartiallySucceeded) {
    ShowStatus(IDS_AHOI_DEVELOPER_COOKIE_BATCH_PARTIAL);
  } else {
    ShowError(result.error);
  }
  // Batch completion can race external cookie changes. Always reload the
  // browser-process store instead of predicting which rows remain.
  LoadCookies();
}

void DeveloperCookieManagerView::ResetBatchDeleteConfirmation() {
  if (!pending_delete_ids_) {
    return;
  }
  pending_delete_ids_.reset();
  if (delete_visible_button_) {
    delete_visible_button_->SetText(
        l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_DELETE_VISIBLE));
  }
  if (status_label_) {
    status_label_->SetVisible(false);
  }
}

void DeveloperCookieManagerView::SaveEditor() {
  if (busy_ || !adapter_) {
    return;
  }
  ResetBatchDeleteConfirmation();
  SetBusy(true);
  const int success_id = editing_cookie_id_ ? IDS_AHOI_DEVELOPER_COOKIE_SAVED
                                            : IDS_AHOI_DEVELOPER_COOKIE_CREATED;
  if (!adapter_->Save(
          site_url_, editing_cookie_id_, EditorDraft(),
          base::BindOnce(&DeveloperCookieManagerView::OnMutationFinished,
                         weak_ptr_factory_.GetWeakPtr(), success_id))) {
    SetBusy(false);
    ShowError(DeveloperCookieError::kUnavailable);
  }
}

void DeveloperCookieManagerView::CancelEditor() {
  editing_cookie_id_.reset();
  editor_->SetVisible(false);
  scroll_view_->ClipHeightTo(84,
                             visual_style::kDeveloperCookieListMaximumHeight);
  PreferredSizeChanged();
}

void DeveloperCookieManagerView::SetEditorCookie(
    const DeveloperCookie* cookie) {
  name_field_->SetText(base::UTF8ToUTF16(cookie ? cookie->name : ""));
  value_field_->SetText(base::UTF8ToUTF16(cookie ? cookie->value : ""));
  domain_field_->SetText(
      base::UTF8ToUTF16(cookie ? cookie->domain : site_url_.host()));
  path_field_->SetText(base::UTF8ToUTF16(cookie ? cookie->path : "/"));
  secure_checkbox_->SetChecked(cookie && cookie->secure);
  http_only_checkbox_->SetChecked(cookie && cookie->http_only);
  partitioned_checkbox_->SetChecked(cookie && cookie->partitioned);
  same_site_combobox_->SetSelectedIndex(0);
  if (cookie) {
    for (size_t i = 0; i < kSameSiteValues.size(); ++i) {
      if (kSameSiteValues[i] == cookie->same_site) {
        same_site_combobox_->SetSelectedIndex(i);
        break;
      }
    }
  }
  expiration_combobox_->SetSelectedIndex(cookie ? 0u : 1u);
  editor_->SetVisible(true);
  scroll_view_->ClipHeightTo(
      64, visual_style::kDeveloperCookieListEditorMaximumHeight);
  status_label_->SetVisible(false);
  name_field_->RequestFocus();
  PreferredSizeChanged();
}

DeveloperCookieDraft DeveloperCookieManagerView::EditorDraft() const {
  const size_t same_site_index =
      std::min(same_site_combobox_->GetSelectedIndex().value_or(0u),
               kSameSiteValues.size() - 1);
  const size_t expiration_index =
      std::min(expiration_combobox_->GetSelectedIndex().value_or(1u),
               kExpirationValues.size() - 1);
  return DeveloperCookieDraft{
      .name = base::UTF16ToUTF8(name_field_->GetText()),
      .value = base::UTF16ToUTF8(value_field_->GetText()),
      .domain = base::UTF16ToUTF8(domain_field_->GetText()),
      .path = base::UTF16ToUTF8(path_field_->GetText()),
      .secure = secure_checkbox_->GetChecked(),
      .http_only = http_only_checkbox_->GetChecked(),
      .partitioned = partitioned_checkbox_->GetChecked(),
      .same_site = kSameSiteValues[same_site_index],
      .expiration = kExpirationValues[expiration_index],
  };
}

void DeveloperCookieManagerView::OnMutationFinished(
    int success_string_id,
    DeveloperCookieResult result) {
  SetBusy(false);
  if (!result.succeeded()) {
    ShowError(result.error);
    if (result.error == DeveloperCookieError::kPartiallySucceeded) {
      LoadCookies();
    }
    return;
  }
  ShowStatus(success_string_id);
  editing_cookie_id_.reset();
  editor_->SetVisible(false);
  scroll_view_->ClipHeightTo(84,
                             visual_style::kDeveloperCookieListMaximumHeight);
  LoadCookies();
}

void DeveloperCookieManagerView::SetBusy(bool busy) {
  busy_ = busy;
  add_button_->SetEnabled(!busy);
  rows_container_->SetEnabled(!busy);
  editor_->SetEnabled(!busy);
  if (save_button_) {
    save_button_->SetEnabled(!busy);
  }
  search_field_->SetEnabled(!busy);
  if (delete_visible_button_) {
    delete_visible_button_->SetEnabled(!busy && !VisibleCookieIds().empty());
  }
}

void DeveloperCookieManagerView::ShowStatus(int string_id) {
  status_label_->SetText(l10n_util::GetStringUTF16(string_id));
  status_label_->SetVisible(true);
  PreferredSizeChanged();
}

void DeveloperCookieManagerView::ShowError(DeveloperCookieError error) {
  if (error == DeveloperCookieError::kUnavailable && empty_label_) {
    empty_label_->SetVisible(false);
  }
  ShowStatus(ErrorStringId(error));
}

const DeveloperCookie* DeveloperCookieManagerView::FindCookie(
    uint64_t cookie_id) const {
  for (const DeveloperCookie& cookie : cookies_) {
    if (cookie.id == cookie_id) {
      return &cookie;
    }
  }
  return nullptr;
}

void DeveloperCookieManagerView::OnAppearanceChanged(
    const appearance::GlassPolicy& policy) {
  const appearance::SurfaceAppearance surface =
      appearance::AppearanceResolver::Resolve(
          appearance::SurfaceRole::kDeveloperTools, policy);
  views::ClientView* client_view =
      GetWidget() ? GetWidget()->client_view() : nullptr;
  if (!client_view) {
    appearance::ApplySurfaceAppearance(this, surface);
    return;
  }
  appearance::ClearSurfaceBackgroundAppearance(this);
  appearance::ApplySurfaceBackgroundAppearance(client_view, surface);
}

}  // namespace ahoi
