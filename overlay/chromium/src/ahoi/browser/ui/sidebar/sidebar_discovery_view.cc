// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_discovery_view.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/string_search.h"
#include "base/location.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"

namespace ahoi::sidebar {

namespace {

// Search must see beyond a busy cross-workspace result head so current-
// workspace tree matches are not accidentally omitted from the inline
// projection. Only the compact supplemental sections are rendered below.
constexpr size_t kMaxSearchResults = 256u;
constexpr size_t kMaxRecentlyClosedResults = 24u;
constexpr size_t kMaxSupplementalSearchResults = 4u;
constexpr size_t kMaxSupplementalRecentlyClosedResults = 4u;
constexpr size_t kMaxFilteredRecentlyClosedResults = 3u;
constexpr int kSupplementalResultRowHeight = 46;
bool IsRecentlyClosed(SidebarDiscoveryItemKind kind) {
  return kind == SidebarDiscoveryItemKind::kRecentlyClosedTab ||
         kind == SidebarDiscoveryItemKind::kRecentlyClosedSplit ||
         kind == SidebarDiscoveryItemKind::kRecentlyClosedGroup ||
         kind == SidebarDiscoveryItemKind::kRecentlyClosedWindow;
}

std::u16string TypeLabelForItem(SidebarDiscoveryItemKind kind) {
  int message_id = 0;
  switch (kind) {
    case SidebarDiscoveryItemKind::kOpenTab:
      message_id = IDS_AHOI_SIDEBAR_DISCOVERY_OPEN_TAB;
      break;
    case SidebarDiscoveryItemKind::kSleepingTab:
      message_id = IDS_AHOI_SIDEBAR_DISCOVERY_SLEEPING_TAB;
      break;
    case SidebarDiscoveryItemKind::kSavedPage:
      message_id = IDS_AHOI_SIDEBAR_DISCOVERY_SAVED_PAGE;
      break;
    case SidebarDiscoveryItemKind::kFolder:
      message_id = IDS_AHOI_SIDEBAR_DISCOVERY_GROUP;
      break;
    case SidebarDiscoveryItemKind::kWorkspace:
      message_id = IDS_AHOI_SIDEBAR_DISCOVERY_WORKSPACE;
      break;
    case SidebarDiscoveryItemKind::kDeviceTab:
      message_id = IDS_AHOI_SIDEBAR_DISCOVERY_DEVICE_TAB;
      break;
    case SidebarDiscoveryItemKind::kRecentlyClosedTab:
      message_id = IDS_AHOI_SIDEBAR_DISCOVERY_RECENT_TAB;
      break;
    case SidebarDiscoveryItemKind::kRecentlyClosedSplit:
      message_id = IDS_AHOI_SIDEBAR_DISCOVERY_RECENT_SPLIT;
      break;
    case SidebarDiscoveryItemKind::kRecentlyClosedGroup:
      message_id = IDS_AHOI_SIDEBAR_DISCOVERY_RECENT_GROUP;
      break;
    case SidebarDiscoveryItemKind::kRecentlyClosedWindow:
      message_id = IDS_AHOI_SIDEBAR_DISCOVERY_RECENT_WINDOW;
      break;
  }
  return l10n_util::GetStringUTF16(message_id);
}

bool MatchesQuery(const SidebarDiscoveryItem& item,
                  const std::u16string& query) {
  if (query.empty()) {
    return true;
  }
  return base::i18n::StringSearchIgnoringCaseAndAccents(query, item.title,
                                                        nullptr, nullptr) ||
         base::i18n::StringSearchIgnoringCaseAndAccents(
             query, item.secondary_text, nullptr, nullptr) ||
         base::i18n::StringSearchIgnoringCaseAndAccents(
             query, TypeLabelForItem(item.kind), nullptr, nullptr);
}

const gfx::VectorIcon& IconForItem(SidebarDiscoveryItemKind kind) {
  if (IsRecentlyClosed(kind)) {
    return vector_icons::kHistoryIcon;
  }
  switch (kind) {
    case SidebarDiscoveryItemKind::kFolder:
      return vector_icons::kFolderFlippableIcon;
    case SidebarDiscoveryItemKind::kOpenTab:
    case SidebarDiscoveryItemKind::kSleepingTab:
    case SidebarDiscoveryItemKind::kSavedPage:
      return vector_icons::kGlobeIcon;
    case SidebarDiscoveryItemKind::kDeviceTab:
      return vector_icons::kDevicesIcon;
    case SidebarDiscoveryItemKind::kWorkspace:
      return vector_icons::kSearchIcon;
    case SidebarDiscoveryItemKind::kRecentlyClosedTab:
    case SidebarDiscoveryItemKind::kRecentlyClosedSplit:
    case SidebarDiscoveryItemKind::kRecentlyClosedGroup:
    case SidebarDiscoveryItemKind::kRecentlyClosedWindow:
      break;
  }
  return vector_icons::kSearchIcon;
}

std::u16string ItemSecondaryText(const SidebarDiscoveryItem& item) {
  // The section heading already provides the origin for restored entries.
  // Repeating "recently closed tab" on every compact row crowds out the
  // useful URL and relative time, especially at the minimum sidebar width.
  std::u16string secondary = IsRecentlyClosed(item.kind)
                                 ? std::u16string()
                                 : TypeLabelForItem(item.kind);
  const auto append = [&secondary](std::u16string text) {
    if (text.empty()) {
      return;
    }
    if (!secondary.empty()) {
      secondary.append(u"  ·  ");
    }
    secondary.append(text);
  };
  append(item.secondary_text);
  if (item.tab_count > 1u) {
    append(l10n_util::GetPluralStringFUTF16(IDS_TAB_SEARCH_TAB_COUNT,
                                            static_cast<int>(item.tab_count)));
  }
  if (IsRecentlyClosed(item.kind) && !item.timestamp.is_null()) {
    const base::TimeDelta elapsed =
        std::max(base::TimeDelta(), base::Time::Now() - item.timestamp);
    append(ui::TimeFormat::SimpleWithMonthAndYear(
        ui::TimeFormat::FORMAT_ELAPSED, ui::TimeFormat::LENGTH_SHORT, elapsed,
        true));
  }
  return secondary;
}

std::u16string AccessibleItemName(const SidebarDiscoveryItem& item) {
  std::u16string name = item.title;
  const std::u16string secondary = ItemSecondaryText(item);
  if (!secondary.empty()) {
    name.append(u" — ");
    name.append(secondary);
  }
  return name;
}

}  // namespace

class SidebarDiscoveryResultRow final : public views::Button {
  METADATA_HEADER(SidebarDiscoveryResultRow, views::Button)

 public:
  using SelectedCallback = base::RepeatingClosure;
  using KeyCallback = base::RepeatingCallback<bool(const ui::KeyEvent&)>;

  SidebarDiscoveryResultRow(const SidebarDiscoveryItem& item,
                            PressedCallback pressed_callback,
                            SelectedCallback selected_callback,
                            KeyCallback key_callback)
      : views::Button(std::move(pressed_callback)),
        selected_callback_(std::move(selected_callback)),
        key_callback_(std::move(key_callback)) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetHasInkDropActionOnClick(false);
    SetShowInkDropWhenHotTracked(false);
    SetPreferredSize(gfx::Size(0, kSupplementalResultRowHeight));
    SetAccessibleName(AccessibleItemName(item));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(4, 9), 9));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* icon = AddChildView(
        std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            IconForItem(item.kind), visual_style::kMutedText, 17)));
    icon->SetPreferredSize(gfx::Size(20, 20));
    icon->SetImageSize(gfx::Size(17, 17));
    icon->SetCanProcessEventsWithinSubtree(false);
    icon->GetViewAccessibility().SetIsIgnored(true);

    auto text = std::make_unique<views::View>();
    auto* text_layout =
        text->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 1));
    text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    auto* title =
        text->AddChildView(std::make_unique<views::Label>(item.title));
    title->SetSubpixelRenderingEnabled(false);
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetElideBehavior(gfx::ELIDE_TAIL);
    title->SetEnabledColor(visual_style::kText);
    title->GetViewAccessibility().SetIsIgnored(true);
    const std::u16string secondary_text = ItemSecondaryText(item);
    auto* secondary =
        text->AddChildView(std::make_unique<views::Label>(secondary_text));
    secondary->SetSubpixelRenderingEnabled(false);
    secondary->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    secondary->SetElideBehavior(gfx::ELIDE_TAIL);
    secondary->SetEnabledColor(visual_style::kMutedText);
    secondary->SetVisible(!secondary_text.empty());
    secondary->GetViewAccessibility().SetIsIgnored(true);
    views::View* const text_ptr = AddChildView(std::move(text));
    layout->SetFlexForView(text_ptr, 1);
    UpdateBackground();
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    UpdateBackground();
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    return key_callback_.Run(event) || views::Button::OnKeyPressed(event);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    views::Button::OnMouseEntered(event);
    selected_callback_.Run();
  }

  void OnFocus() override {
    views::Button::OnFocus();
    selected_callback_.Run();
    UpdateBackground();
  }

  void OnBlur() override {
    views::Button::OnBlur();
    UpdateBackground();
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateBackground();
  }

 private:
  void UpdateBackground() {
    const bool hovered = GetState() == ButtonState::STATE_HOVERED ||
                         GetState() == ButtonState::STATE_PRESSED;
    SetBackground(selected_ || HasFocus() ? views::CreateRoundedRectBackground(
                                                visual_style::kSelectedSurface,
                                                visual_style::kRowCornerRadius)
                  : hovered               ? views::CreateRoundedRectBackground(
                                                visual_style::kHoverSurface,
                                                visual_style::kRowCornerRadius)
                                          : nullptr);
  }

  const SelectedCallback selected_callback_;
  const KeyCallback key_callback_;
  bool selected_ = false;
};

BEGIN_METADATA(SidebarDiscoveryResultRow)
END_METADATA

void SidebarDiscoveryView::Open() {
  if (!is_open_) {
    is_open_ = true;
    input_shell_->SetVisible(true);
  }
  const base::WeakPtr<SidebarDiscoveryView> weak_this =
      weak_ptr_factory_.GetWeakPtr();
  RefreshResults();
  if (!weak_this || !is_open_) {
    return;
  }
  search_field_->RequestFocus();
  search_field_->SelectAll(/*reversed=*/false);
}

void SidebarDiscoveryView::Close() {
  is_open_ = false;
  const base::WeakPtr<SidebarDiscoveryView> weak_this =
      weak_ptr_factory_.GetWeakPtr();
  Reset();
  if (!weak_this || is_open_) {
    return;
  }
  input_shell_->SetVisible(false);
  no_results_label_->SetVisible(false);
  supplemental_section_->SetVisible(false);
  recently_closed_section_->SetVisible(false);
  InvalidateLayout();
  PreferredSizeChanged();
}

void SidebarDiscoveryView::Reset() {
  suppress_contents_refresh_ = true;
  search_field_->SetText(std::u16string());
  suppress_contents_refresh_ = false;
  selected_index_.reset();
  search_field_->GetViewAccessibility().ClearActiveDescendant();
  RefreshResults();
}

bool SidebarDiscoveryView::CloseOrClear() {
  if (!search_field_->GetText().empty()) {
    const bool restore_search_focus = !search_field_->HasFocus();
    const base::WeakPtr<SidebarDiscoveryView> weak_this =
        weak_ptr_factory_.GetWeakPtr();
    Reset();
    if (!weak_this) {
      return true;
    }
    if (restore_search_focus) {
      search_field_->RequestFocus();
    }
    return true;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<SidebarDiscoveryView> view) {
                       if (view) {
                         view->close_callback_.Run();
                       }
                     },
                     weak_ptr_factory_.GetWeakPtr()));
  return true;
}

void SidebarDiscoveryView::InvalidatePrimaryResultSelection() {
  primary_selection_active_ = false;
}

void SidebarDiscoveryView::ContentsChanged(views::Textfield* sender,
                                           const std::u16string& new_contents) {
  CHECK_EQ(sender, search_field_);
  if (!suppress_contents_refresh_) {
    RefreshResults();
  }
}

bool SidebarDiscoveryView::HandleKeyEvent(views::Textfield* sender,
                                          const ui::KeyEvent& key_event) {
  if (sender != search_field_ ||
      key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  switch (key_event.key_code()) {
    case ui::VKEY_DOWN:
      return MoveSelection(1, /*request_focus=*/false);
    case ui::VKEY_UP:
      return MoveSelection(-1, /*request_focus=*/false);
    case ui::VKEY_RETURN:
      return AcceptSelection();
    case ui::VKEY_ESCAPE:
      return CloseOrClear();
    default:
      return false;
  }
}

void SidebarDiscoveryView::OnSidebarDiscoveryModelChanged() {
  if (is_open_) {
    RefreshResults();
  }
}

void SidebarDiscoveryView::RefreshResults() {
  const std::optional<std::string> selected_stable_id =
      selected_index_.has_value() && *selected_index_ < items_.size()
          ? std::optional<std::string>(items_[*selected_index_].stable_id)
          : std::nullopt;
  ClearPrimarySelection();
  search_field_->GetViewAccessibility().ClearActiveDescendant();
  selected_index_.reset();
  rows_.clear();
  supplemental_results_container_->RemoveAllChildViews();
  recently_closed_results_container_->RemoveAllChildViews();

  const std::u16string raw_query(search_field_->GetText());
  const std::u16string query(base::TrimWhitespace(raw_query, base::TRIM_ALL));
  const std::u16string close_action = l10n_util::GetStringUTF16(
      raw_query.empty() ? IDS_CLOSE : IDS_CLEAR_SEARCH);
  close_button_->SetAccessibleName(close_action);
  close_button_->SetTooltipText(close_action);
  const std::u16string supplemental_section_name =
      query.empty()
          ? l10n_util::GetStringUTF16(IDS_AHOI_SIDEBAR_DISCOVERY_SEARCH)
          : l10n_util::GetStringFUTF16(IDS_SEARCH_RESULTS, query);
  supplemental_section_label_->SetText(supplemental_section_name);
  supplemental_results_container_->GetViewAccessibility().SetName(
      supplemental_section_name);

  std::vector<SidebarDiscoveryItem> search_results;
  if (!query.empty()) {
    search_results = model_->Search(query, kMaxSearchResults);
  }
  const base::WeakPtr<SidebarDiscoveryView> weak_this =
      weak_ptr_factory_.GetWeakPtr();
  const std::set<std::string> consumed_stable_ids =
      filter_callback_.Run(query, search_results);
  if (!weak_this) {
    return;
  }
  if (!is_open_) {
    items_.clear();
    supplemental_result_count_ = 0;
    no_results_label_->SetVisible(false);
    supplemental_section_->SetVisible(false);
    recently_closed_section_->SetVisible(false);
    supplemental_results_container_->InvalidateLayout();
    supplemental_results_scroll_view_->InvalidateLayout();
    recently_closed_results_container_->InvalidateLayout();
    recently_closed_results_scroll_view_->InvalidateLayout();
    InvalidateLayout();
    PreferredSizeChanged();
    return;
  }

  items_.clear();
  items_.reserve(kMaxSupplementalSearchResults +
                 kMaxSupplementalRecentlyClosedResults);
  if (!query.empty()) {
    for (const SidebarDiscoveryItem& item : search_results) {
      if (IsRecentlyClosed(item.kind) ||
          consumed_stable_ids.contains(item.stable_id)) {
        continue;
      }
      items_.push_back(item);
      if (items_.size() == kMaxSupplementalSearchResults) {
        break;
      }
    }
  }
  supplemental_result_count_ = items_.size();

  std::set<std::string> listed_stable_ids;
  for (const SidebarDiscoveryItem& item : items_) {
    listed_stable_ids.insert(item.stable_id);
  }
  size_t recently_closed_count = 0;
  const size_t recently_closed_limit =
      query.empty() ? kMaxSupplementalRecentlyClosedResults
                    : kMaxFilteredRecentlyClosedResults;
  for (SidebarDiscoveryItem& item :
       model_->RecentlyClosed(kMaxRecentlyClosedResults)) {
    if (!MatchesQuery(item, query) ||
        listed_stable_ids.contains(item.stable_id)) {
      continue;
    }
    listed_stable_ids.insert(item.stable_id);
    items_.push_back(std::move(item));
    ++recently_closed_count;
    if (recently_closed_count == recently_closed_limit) {
      break;
    }
  }

  const bool show_empty_state =
      !query.empty() && consumed_stable_ids.empty() && items_.empty();
  for (size_t index = 0; index < items_.size(); ++index) {
    const SidebarDiscoveryItem& item = items_[index];
    views::View* const container =
        index < supplemental_result_count_
            ? supplemental_results_container_.get()
            : recently_closed_results_container_.get();
    const size_t section_index = index < supplemental_result_count_
                                     ? index
                                     : index - supplemental_result_count_;
    const size_t section_size = index < supplemental_result_count_
                                    ? supplemental_result_count_
                                    : recently_closed_count;
    const std::string stable_id = item.stable_id;
    auto row = std::make_unique<SidebarDiscoveryResultRow>(
        item,
        base::BindRepeating(
            [](base::WeakPtr<SidebarDiscoveryView> view,
               std::string item_stable_id, const ui::Event&) {
              if (view) {
                view->ScheduleAcceptStableId(std::move(item_stable_id));
              }
            },
            weak_ptr_factory_.GetWeakPtr(), stable_id),
        base::BindRepeating(&SidebarDiscoveryView::OnResultHovered,
                            weak_ptr_factory_.GetWeakPtr(), stable_id),
        base::BindRepeating(
            [](base::WeakPtr<SidebarDiscoveryView> view,
               const std::string& item_stable_id, const ui::KeyEvent& event) {
              return view && view->HandleResultKeyEvent(item_stable_id, event);
            },
            weak_ptr_factory_.GetWeakPtr(), stable_id));
    row->GetViewAccessibility().SetPosInSet(
        static_cast<int>(section_index + 1u));
    row->GetViewAccessibility().SetSetSize(static_cast<int>(section_size));
    rows_.push_back(container->AddChildView(std::move(row)));
  }

  std::optional<size_t> selection;
  if (selected_stable_id.has_value()) {
    selection = FindItemIndex(*selected_stable_id);
  }
  SelectIndex(selection, /*request_focus=*/false);
  no_results_label_->SetVisible(show_empty_state);
  supplemental_section_->SetVisible(supplemental_result_count_ > 0u);
  recently_closed_section_->SetVisible(recently_closed_count > 0u);
  supplemental_results_container_->InvalidateLayout();
  supplemental_results_scroll_view_->InvalidateLayout();
  recently_closed_results_container_->InvalidateLayout();
  recently_closed_results_scroll_view_->InvalidateLayout();
  InvalidateLayout();
  PreferredSizeChanged();
}

void SidebarDiscoveryView::SelectIndex(std::optional<size_t> index,
                                       bool request_focus) {
  if (index.has_value() && *index >= rows_.size()) {
    index.reset();
  }
  if (selected_index_.has_value() && *selected_index_ < rows_.size()) {
    rows_[*selected_index_]->SetSelected(false);
  }
  if (index.has_value()) {
    ClearPrimarySelection();
  }
  selected_index_ = index;
  if (!selected_index_.has_value()) {
    search_field_->GetViewAccessibility().ClearActiveDescendant();
    return;
  }
  SidebarDiscoveryResultRow* const row = rows_[*selected_index_];
  row->SetSelected(true);
  row->ScrollViewToVisible();
  search_field_->GetViewAccessibility().SetActiveDescendant(*row);
  if (request_focus) {
    row->RequestFocus();
  }
}

bool SidebarDiscoveryView::MoveSelection(int delta, bool request_focus) {
  CHECK(delta == -1 || delta == 1);
  if (primary_selection_active_) {
    const PrimaryResultAction action =
        delta > 0 ? PrimaryResultAction::kSelectNext
                  : PrimaryResultAction::kSelectPrevious;
    if (RunPrimaryResultAction(action)) {
      return true;
    }
    // The host leaves the edge item selected when it reports that movement
    // cannot continue. Explicitly clear that transient highlight before
    // entering the supplemental sections.
    ClearPrimarySelection();
    if (rows_.empty()) {
      return true;
    }
    SelectIndex(delta > 0 ? 0u : rows_.size() - 1u, request_focus);
    return true;
  }

  if (selected_index_.has_value()) {
    const int selected = static_cast<int>(*selected_index_);
    const int next = selected + delta;
    if (next >= 0 && next < static_cast<int>(rows_.size())) {
      SelectIndex(static_cast<size_t>(next), request_focus);
      return true;
    }
    SelectIndex(std::nullopt, /*request_focus=*/false);
    const PrimaryResultAction action = delta > 0
                                           ? PrimaryResultAction::kSelectFirst
                                           : PrimaryResultAction::kSelectLast;
    primary_selection_active_ = RunPrimaryResultAction(action);
    if (primary_selection_active_ && request_focus) {
      // Primary rows deliberately keep focus in the search field so further
      // Up/Down events continue through the unified result order. Otherwise
      // the previously focused supplemental row would handle the next key and
      // jump back to the same boundary.
      search_field_->RequestFocus();
    }
    return true;
  }

  // Down follows the visual order: primary hierarchy first, supplemental
  // sources second. Up starts at the final visible item.
  if (delta > 0) {
    primary_selection_active_ =
        RunPrimaryResultAction(PrimaryResultAction::kSelectFirst);
    if (!primary_selection_active_ && !rows_.empty()) {
      SelectIndex(0u, request_focus);
    }
  } else if (!rows_.empty()) {
    SelectIndex(rows_.size() - 1u, request_focus);
  } else {
    primary_selection_active_ =
        RunPrimaryResultAction(PrimaryResultAction::kSelectLast);
  }
  return primary_selection_active_ || selected_index_.has_value();
}

bool SidebarDiscoveryView::AcceptSelection() {
  if (selected_index_.has_value() && *selected_index_ < items_.size()) {
    ScheduleAcceptStableId(items_[*selected_index_].stable_id);
    return true;
  }
  return primary_selection_active_ &&
         RunPrimaryResultAction(PrimaryResultAction::kActivateSelection);
}

bool SidebarDiscoveryView::RunPrimaryResultAction(PrimaryResultAction action) {
  return primary_result_callback_.Run(action);
}

void SidebarDiscoveryView::ClearPrimarySelection() {
  if (!primary_selection_active_) {
    return;
  }
  primary_selection_active_ = false;
  primary_result_callback_.Run(PrimaryResultAction::kClearSelection);
}

bool SidebarDiscoveryView::HandleResultKeyEvent(const std::string& stable_id,
                                                const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  switch (event.key_code()) {
    case ui::VKEY_DOWN:
      SelectIndex(FindItemIndex(stable_id), /*request_focus=*/false);
      return MoveSelection(1, /*request_focus=*/true);
    case ui::VKEY_UP:
      SelectIndex(FindItemIndex(stable_id), /*request_focus=*/false);
      return MoveSelection(-1, /*request_focus=*/true);
    case ui::VKEY_RETURN:
      ScheduleAcceptStableId(stable_id);
      return true;
    case ui::VKEY_ESCAPE:
      // Clearing the query rebuilds the result list. Do that after this row
      // has finished dispatching its key event so it cannot delete itself on
      // the current stack.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(
                         [](base::WeakPtr<SidebarDiscoveryView> view) {
                           if (view) {
                             view->CloseOrClear();
                           }
                         },
                         weak_ptr_factory_.GetWeakPtr()));
      return true;
    default:
      return false;
  }
}

void SidebarDiscoveryView::OnResultHovered(const std::string& stable_id) {
  SelectIndex(FindItemIndex(stable_id), /*request_focus=*/false);
}

void SidebarDiscoveryView::ScheduleAcceptStableId(std::string stable_id) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&SidebarDiscoveryView::AcceptStableId,
                     weak_ptr_factory_.GetWeakPtr(), std::move(stable_id)));
}

void SidebarDiscoveryView::AcceptStableId(const std::string& stable_id) {
  const std::optional<size_t> index = FindItemIndex(stable_id);
  if (!index.has_value()) {
    return;
  }
  const SidebarDiscoveryItem item = items_[*index];
  const ActivateCommandCallback activate_callback = activate_command_callback_;
  const RestoreCallback restore_callback = restore_callback_;
  const CloseCallback close_callback = close_callback_;
  const base::WeakPtr<SidebarDiscoveryView> weak_this =
      weak_ptr_factory_.GetWeakPtr();
  bool activated = false;
  if (item.command.has_value()) {
    activated = activate_callback.Run(*item.command);
  } else if (item.restore_id.has_value()) {
    activated = restore_callback.Run(*item.restore_id);
  }
  // Activating a cross-window tab can synchronously tear down this sidebar.
  if (weak_this && weak_this->is_open_ && activated) {
    close_callback.Run();
  }
}

std::optional<size_t> SidebarDiscoveryView::FindItemIndex(
    const std::string& stable_id) const {
  const auto it =
      std::ranges::find(items_, stable_id, &SidebarDiscoveryItem::stable_id);
  return it == items_.end() ? std::nullopt
                            : std::optional<size_t>(static_cast<size_t>(
                                  std::distance(items_.begin(), it)));
}

BEGIN_METADATA(SidebarDiscoveryView)
END_METADATA

}  // namespace ahoi::sidebar
