// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_discovery_view.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/grit/generated_resources.h"
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
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"

namespace ahoi::sidebar {

namespace {

constexpr size_t kMaxSearchResults = 64u;
constexpr size_t kMaxRecentlyClosedResults = 24u;

bool IsRecentlyClosed(SidebarDiscoveryItemKind kind) {
  return kind == SidebarDiscoveryItemKind::kRecentlyClosedTab ||
         kind == SidebarDiscoveryItemKind::kRecentlyClosedSplit ||
         kind == SidebarDiscoveryItemKind::kRecentlyClosedGroup ||
         kind == SidebarDiscoveryItemKind::kRecentlyClosedWindow;
}

const gfx::VectorIcon& IconForItem(SidebarDiscoveryItemKind kind) {
  if (IsRecentlyClosed(kind)) {
    return vector_icons::kHistoryIcon;
  }
  switch (kind) {
    case SidebarDiscoveryItemKind::kFolder:
      return vector_icons::kFolderFlippableIcon;
    case SidebarDiscoveryItemKind::kOpenTab:
    case SidebarDiscoveryItemKind::kSavedPage:
      return vector_icons::kGlobeIcon;
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
  std::u16string secondary = item.secondary_text;
  const auto append = [&secondary](std::u16string text) {
    if (text.empty()) {
      return;
    }
    if (!secondary.empty()) {
      secondary.append(u"  ·  ");
    }
    secondary.append(text);
  };
  if (item.tab_count > 1u) {
    append(l10n_util::GetPluralStringFUTF16(
        IDS_TAB_SEARCH_TAB_COUNT, static_cast<int>(item.tab_count)));
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

class SidebarDiscoveryCloseButton final : public views::Button {
  METADATA_HEADER(SidebarDiscoveryCloseButton, views::Button)

 public:
  explicit SidebarDiscoveryCloseButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetHasInkDropActionOnClick(false);
    SetShowInkDropWhenHotTracked(false);
    SetPreferredSize(gfx::Size(28, 28));
    const std::u16string close = l10n_util::GetStringUTF16(IDS_CLOSE);
    SetAccessibleName(close);
    SetTooltipText(close);
    icon_ = AddChildView(
        std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            vector_icons::kCloseIcon, visual_style::kMutedText, 16)));
    icon_->SetCanProcessEventsWithinSubtree(false);
    icon_->GetViewAccessibility().SetIsIgnored(true);
  }

  void Layout(PassKey) override { icon_->SetBoundsRect(GetLocalBounds()); }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    const bool highlighted = GetState() == ButtonState::STATE_HOVERED ||
                             GetState() == ButtonState::STATE_PRESSED ||
                             HasFocus();
    SetBackground(highlighted ? views::CreateRoundedRectBackground(
                                    visual_style::kHoverSurface,
                                    visual_style::kControlCornerRadius)
                              : nullptr);
  }

 private:
  raw_ptr<views::ImageView> icon_ = nullptr;
};

BEGIN_METADATA(SidebarDiscoveryCloseButton)
END_METADATA

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
    SetPreferredSize(gfx::Size(0, 50));
    SetAccessibleName(AccessibleItemName(item));
    GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(6, 9), 9));
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
    auto* secondary = text->AddChildView(
        std::make_unique<views::Label>(secondary_text));
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
    GetViewAccessibility().SetIsSelected(selected_);
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
    SetBackground(selected_ || HasFocus()
                      ? views::CreateRoundedRectBackground(
                            visual_style::kSelectedSurface,
                            visual_style::kRowCornerRadius)
                  : hovered ? views::CreateRoundedRectBackground(
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

SidebarDiscoveryView::SidebarDiscoveryView(
    SidebarDiscoveryModel* model,
    ActivateCommandCallback activate_command_callback,
    RestoreCallback restore_callback,
    CloseCallback close_callback)
    : model_(model),
      activate_command_callback_(std::move(activate_command_callback)),
      restore_callback_(std::move(restore_callback)),
      close_callback_(std::move(close_callback)) {
  CHECK(model_);
  CHECK(activate_command_callback_);
  CHECK(restore_callback_);
  CHECK(close_callback_);
  model_->AddObserver(this);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);

  auto input_shell = std::make_unique<views::View>();
  input_shell->SetPreferredSize(gfx::Size(0, 40));
  input_shell->SetBackground(views::CreateRoundedRectBackground(
      visual_style::kRaisedSurface, visual_style::kControlCornerRadius));
  input_shell->SetBorder(views::CreateRoundedRectBorder(
      visual_style::kControlBorderThickness,
      visual_style::kControlCornerRadius, visual_style::kDivider));
  auto* input_layout =
      input_shell->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 9),
          6));
  input_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* search_icon = input_shell->AddChildView(
      std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
          vector_icons::kSearchIcon, visual_style::kMutedText, 17)));
  search_icon->SetPreferredSize(gfx::Size(20, 20));
  search_icon->SetCanProcessEventsWithinSubtree(false);
  search_icon->GetViewAccessibility().SetIsIgnored(true);

  search_field_ =
      input_shell->AddChildView(std::make_unique<views::Textfield>());
  const std::u16string placeholder =
      l10n_util::GetStringUTF16(IDS_TAB_SEARCH_SEARCH_TABS);
  search_field_->SetController(this);
  search_field_->SetPlaceholderText(placeholder);
  search_field_->SetAccessibleName(placeholder);
  search_field_->SetFocusBehavior(FocusBehavior::ALWAYS);
  search_field_->SetBorder(nullptr);
  search_field_->SetBackgroundColor(visual_style::kRaisedSurface);
  search_field_->SetTextColorId(visual_style::kText);
  search_field_->SetPlaceholderTextColorId(visual_style::kMutedText);
  search_field_->RemoveHoverEffect();
  views::FocusRing::Install(search_field_);
  views::FocusRing::Get(search_field_)->SetOutsetFocusRingDisabled(true);
  input_layout->SetFlexForView(search_field_, 1);
  input_shell->AddChildView(std::make_unique<SidebarDiscoveryCloseButton>(
      base::BindRepeating(
          [](base::WeakPtr<SidebarDiscoveryView> view, const ui::Event&) {
            if (view) {
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](base::WeakPtr<SidebarDiscoveryView> pending_view) {
                        if (pending_view) {
                          pending_view->close_callback_.Run();
                        }
                      },
                      view));
            }
          },
          weak_ptr_factory_.GetWeakPtr())));
  AddChildView(std::move(input_shell));

  section_label_ = AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_TAB_SEARCH_RECENTLY_CLOSED)));
  section_label_->SetSubpixelRenderingEnabled(false);
  section_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  section_label_->SetEnabledColor(visual_style::kMutedText);
  section_label_->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(2, 8, 0,
                                                                       8)));

  auto results_container = std::make_unique<views::View>();
  auto* results_layout =
      results_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  results_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  results_container->GetViewAccessibility().SetRole(
      ax::mojom::Role::kListBox);
  results_container_ = results_container.get();

  auto scroll = std::make_unique<views::ScrollView>();
  scroll->SetBackground(nullptr);
  scroll->SetDrawOverflowIndicator(false);
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->SetUseContentsPreferredSize(true);
  scroll->SetContents(std::move(results_container));
  results_scroll_view_ = AddChildView(std::move(scroll));
  layout->SetFlexForView(results_scroll_view_, 1, /*use_min_size=*/true);

  const ui::AXPlatformNodeId field_id =
      search_field_->GetViewAccessibility().GetUniqueId();
  const ui::AXPlatformNodeId results_id =
      results_container_->GetViewAccessibility().GetUniqueId();
  results_container_->GetViewAccessibility().SetPopupForId(field_id);
  search_field_->GetViewAccessibility().SetControlIds({results_id});
  search_field_->GetViewAccessibility().SetHasPopup(
      ax::mojom::HasPopup::kListbox);
  search_field_->GetViewAccessibility().SetIsExpanded();
  RefreshResults();
}

SidebarDiscoveryView::~SidebarDiscoveryView() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  if (search_field_) {
    search_field_->SetController(nullptr);
  }
  model_->RemoveObserver(this);
}

void SidebarDiscoveryView::Open() {
  RefreshResults();
  search_field_->RequestFocus();
  search_field_->SelectAll(/*reversed=*/false);
}

void SidebarDiscoveryView::Reset() {
  search_field_->SetText(std::u16string());
  selected_index_.reset();
  search_field_->GetViewAccessibility().ClearActiveDescendant();
}

bool SidebarDiscoveryView::CloseOrClear() {
  if (!search_field_->GetText().empty()) {
    const bool restore_search_focus = !search_field_->HasFocus();
    search_field_->SetText(std::u16string());
    RefreshResults();
    if (restore_search_focus) {
      search_field_->RequestFocus();
    }
    return true;
  }
  close_callback_.Run();
  return true;
}

void SidebarDiscoveryView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  CHECK_EQ(sender, search_field_);
  RefreshResults();
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
    case ui::VKEY_TAB:
      if (key_event.IsCommandDown() || key_event.IsControlDown() ||
          key_event.IsAltDown()) {
        return false;
      }
      return MoveSelection(key_event.IsShiftDown() ? -1 : 1,
                           /*request_focus=*/false);
    case ui::VKEY_RETURN:
      return AcceptSelection();
    case ui::VKEY_ESCAPE:
      return CloseOrClear();
    default:
      return false;
  }
}

void SidebarDiscoveryView::OnSidebarDiscoveryModelChanged() {
  if (GetVisible()) {
    RefreshResults();
  }
}

void SidebarDiscoveryView::RefreshResults() {
  const std::optional<std::string> selected_stable_id =
      selected_index_.has_value() && *selected_index_ < items_.size()
          ? std::optional<std::string>(items_[*selected_index_].stable_id)
          : std::nullopt;
  search_field_->GetViewAccessibility().ClearActiveDescendant();
  selected_index_.reset();
  rows_.clear();
  results_container_->RemoveAllChildViews();

  const std::u16string query(search_field_->GetText());
  section_label_->SetVisible(query.empty());
  results_container_->GetViewAccessibility().SetName(
      query.empty()
          ? l10n_util::GetStringUTF16(IDS_TAB_SEARCH_RECENTLY_CLOSED)
          : l10n_util::GetStringUTF16(IDS_TAB_SEARCH_SEARCH_TABS));
  items_ = query.empty() ? model_->RecentlyClosed(kMaxRecentlyClosedResults)
                         : model_->Search(query, kMaxSearchResults);
  for (const SidebarDiscoveryItem& item : items_) {
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
        base::BindRepeating(&SidebarDiscoveryView::HandleResultKeyEvent,
                            weak_ptr_factory_.GetWeakPtr(), stable_id));
    row->GetViewAccessibility().SetPosInSet(
        static_cast<int>(rows_.size() + 1u));
    row->GetViewAccessibility().SetSetSize(static_cast<int>(items_.size()));
    rows_.push_back(results_container_->AddChildView(std::move(row)));
  }
  if (items_.empty()) {
    auto* empty = results_container_->AddChildView(
        std::make_unique<views::Label>(l10n_util::GetStringUTF16(
            IDS_TAB_SEARCH_NO_RESULTS_FOUND)));
    empty->SetSubpixelRenderingEnabled(false);
    empty->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    empty->SetEnabledColor(visual_style::kMutedText);
    empty->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(16, 9)));
  }

  std::optional<size_t> selection;
  if (selected_stable_id.has_value()) {
    selection = FindItemIndex(*selected_stable_id);
  }
  if (!selection.has_value() && !items_.empty()) {
    selection = 0u;
  }
  SelectIndex(selection, /*request_focus=*/false);
  results_container_->InvalidateLayout();
  results_scroll_view_->InvalidateLayout();
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
  if (rows_.empty()) {
    return false;
  }
  const int count = static_cast<int>(rows_.size());
  int selected = selected_index_.has_value()
                     ? static_cast<int>(*selected_index_)
                     : (delta > 0 ? -1 : 0);
  selected = (selected + delta + count) % count;
  SelectIndex(static_cast<size_t>(selected), request_focus);
  return true;
}

bool SidebarDiscoveryView::AcceptSelection() {
  if (!selected_index_.has_value() || *selected_index_ >= items_.size()) {
    return false;
  }
  ScheduleAcceptStableId(items_[*selected_index_].stable_id);
  return true;
}

bool SidebarDiscoveryView::HandleResultKeyEvent(
    const std::string& stable_id,
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
          FROM_HERE,
          base::BindOnce(
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
      FROM_HERE, base::BindOnce(&SidebarDiscoveryView::AcceptStableId,
                                weak_ptr_factory_.GetWeakPtr(),
                                std::move(stable_id)));
}

void SidebarDiscoveryView::AcceptStableId(const std::string& stable_id) {
  const std::optional<size_t> index = FindItemIndex(stable_id);
  if (!index.has_value()) {
    return;
  }
  const SidebarDiscoveryItem item = items_[*index];
  const ActivateCommandCallback activate_callback =
      activate_command_callback_;
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
  if (weak_this && activated) {
    close_callback.Run();
  }
}

std::optional<size_t> SidebarDiscoveryView::FindItemIndex(
    const std::string& stable_id) const {
  const auto it = std::ranges::find(items_, stable_id,
                                    &SidebarDiscoveryItem::stable_id);
  return it == items_.end()
             ? std::nullopt
             : std::optional<size_t>(
                   static_cast<size_t>(std::distance(items_.begin(), it)));
}

BEGIN_METADATA(SidebarDiscoveryView)
END_METADATA

}  // namespace ahoi::sidebar
