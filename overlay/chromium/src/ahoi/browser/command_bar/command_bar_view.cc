// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/command_bar_view.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/grit/generated_resources.h"
#include "components/omnibox/browser/omnibox_text_util.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/dragdrop/os_exchange_data_provider.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/client_view.h"

namespace ahoi {

namespace {

constexpr size_t kMaximumSuggestionCount = 5u;

std::u16string AccessibleRowName(const CommandBarSuggestion& suggestion) {
  if (suggestion.secondary_text.empty()) {
    return suggestion.title;
  }
  return base::StrCat({suggestion.title, u", ", suggestion.secondary_text});
}

bool HasOnlyTabTraversalModifiers(const ui::KeyEvent& event) {
  return event.key_code() == ui::VKEY_TAB && !event.IsControlDown() &&
         !event.IsAltDown() && !event.IsCommandDown() && !event.IsAltGrDown();
}

bool HasSameSuggestionIdentity(const CommandBarSuggestion& left,
                               const CommandBarSuggestion& right) {
  if (left.kind != right.kind) {
    return false;
  }
  if (left.item.has_value() && right.item.has_value() &&
      !left.item->stable_id.empty() && !right.item->stable_id.empty()) {
    return left.item->type == right.item->type &&
           left.item->stable_id == right.item->stable_id;
  }
  if (left.destination_url.has_value() && right.destination_url.has_value()) {
    return left.destination_url == right.destination_url;
  }
  return left.title == right.title &&
         left.secondary_text == right.secondary_text;
}

}  // namespace

class CommandBarResultRow final : public views::Button {
 public:
  METADATA_HEADER(CommandBarResultRow, views::Button)

 public:
  using KeyCallback = base::RepeatingCallback<bool(const ui::KeyEvent& event)>;

  CommandBarResultRow(const CommandBarSuggestion& suggestion,
                      PressedCallback pressed_callback,
                      base::RepeatingClosure selected_callback,
                      KeyCallback key_callback)
      : Button(std::move(pressed_callback)),
        selected_callback_(std::move(selected_callback)),
        key_callback_(std::move(key_callback)),
        is_active_tab_(suggestion.is_active_tab) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetHasInkDropActionOnClick(false);
    SetShowInkDropWhenHotTracked(false);
    SetPreferredSize(gfx::Size(visual_style::kCommandBarContentWidth,
                               visual_style::kCommandBarResultRowHeight));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(visual_style::kCommandBarResultVerticalInset,
                        visual_style::kCommandBarResultHorizontalInset),
        visual_style::kCommandBarResultTextSpacing));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* icon = AddChildView(std::make_unique<views::ImageView>());
    icon->SetPreferredSize(
        gfx::Size(visual_style::kCommandBarResultIconBoxSize,
                  visual_style::kCommandBarResultIconBoxSize));
    icon->SetImageSize(gfx::Size(visual_style::kCommandBarResultIconSize,
                                 visual_style::kCommandBarResultIconSize));
    icon->SetImage(
        suggestion.icon.IsEmpty()
            ? ui::ImageModel::FromVectorIcon(
                  suggestion.item.has_value() &&
                          suggestion.item->type == CommandItemType::kFolder
                      ? vector_icons::kFolderFlippableIcon
                  : suggestion.kind == CommandBarSuggestionKind::kInputFallback
                      ? vector_icons::kSearchIcon
                      : vector_icons::kGlobeIcon,
                  visual_style::kMutedText,
                  visual_style::kCommandBarResultIconSize)
            : suggestion.icon);
    icon->GetViewAccessibility().SetIsIgnored(true);

    auto* title = AddChildView(std::make_unique<views::Label>(
        suggestion.title, views::style::CONTEXT_LABEL,
        views::style::STYLE_PRIMARY));
    title->SetSubpixelRenderingEnabled(false);
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetEnabledColor(is_active_tab_ ? visual_style::kAccent
                                          : visual_style::kText);
    title->SetElideBehavior(gfx::ELIDE_TAIL);
    title->GetViewAccessibility().SetIsIgnored(true);
    layout->SetFlexForView(title, 1);

    auto* secondary = AddChildView(std::make_unique<views::Label>(
        suggestion.secondary_text, views::style::CONTEXT_LABEL,
        views::style::STYLE_SECONDARY));
    secondary->SetSubpixelRenderingEnabled(false);
    secondary->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    secondary->SetEnabledColor(visual_style::kMutedText);
    secondary->SetElideBehavior(gfx::ELIDE_MIDDLE);
    secondary->SetPreferredSize(
        gfx::Size(visual_style::kCommandBarSecondaryTextMaximumWidth,
                  visual_style::kCommandBarResultIconSize));
    secondary->SetVisible(!suggestion.secondary_text.empty());
    secondary->GetViewAccessibility().SetIsIgnored(true);

    // Reserve this fixed slot for every row so the active marker never moves
    // titles or actions when results are rebuilt. The marker remains visible
    // while hover and keyboard selection use their own surface states.
    auto* active_tab_indicator =
        AddChildView(std::make_unique<views::ImageView>());
    active_tab_indicator->SetPreferredSize(
        gfx::Size(visual_style::kCommandBarResultIconBoxSize,
                  visual_style::kCommandBarResultIconBoxSize));
    active_tab_indicator->SetImageSize(
        gfx::Size(visual_style::kCommandBarResultIconSize,
                  visual_style::kCommandBarResultIconSize));
    if (is_active_tab_) {
      active_tab_indicator->SetImage(ui::ImageModel::FromVectorIcon(
          vector_icons::kCheckCircleFilledIcon, visual_style::kAccent,
          visual_style::kCommandBarResultIconSize));
    }
    active_tab_indicator->GetViewAccessibility().SetIsIgnored(true);

    auto* accept_hint = AddChildView(std::make_unique<views::Label>(
        u"↵", views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
    accept_hint->SetSubpixelRenderingEnabled(false);
    accept_hint->SetEnabledColor(visual_style::kMutedText);
    accept_hint->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    accept_hint->SetPreferredSize(
        gfx::Size(visual_style::kCommandBarAcceptHintWidth,
                  visual_style::kCommandBarAcceptHintHeight));
    accept_hint->GetViewAccessibility().SetIsIgnored(true);

    GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);
    GetViewAccessibility().SetName(AccessibleRowName(suggestion));
    if (is_active_tab_) {
      GetViewAccessibility().SetDescription(
          l10n_util::GetStringUTF16(IDS_AHOI_COMMAND_BAR_CURRENT_TAB));
    }
    UpdateBackground();
  }

  void SetCommandBarSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    GetViewAccessibility().SetIsSelected(selected_);
    UpdateBackground();
  }

  bool selected_for_testing() const { return selected_; }

  // views::View:
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
    const bool selected_or_focused = selected_ || HasFocus();
    const bool hovered = GetState() == ButtonState::STATE_HOVERED;
    const bool pressed = GetState() == ButtonState::STATE_PRESSED;
    // Arc's command palette reads as one continuous surface. Individual rows
    // stay transparent unless they carry semantic or interaction state. Hover
    // intentionally takes precedence over the selection that mouse entry also
    // updates, keeping pointer hover distinct from keyboard selection.
    std::optional<ui::ColorId> surface;
    if (pressed) {
      surface = visual_style::kSelectedSurface;
    } else if (hovered) {
      surface = visual_style::kHoverSurface;
    } else if (selected_or_focused) {
      surface = visual_style::kSelectedSurface;
    } else if (is_active_tab_) {
      surface = visual_style::kRaisedSurface;
    }
    SetBackground(surface.has_value()
                      ? views::CreateRoundedRectBackground(
                            *surface, visual_style::kRowCornerRadius)
                      : nullptr);
  }

  base::RepeatingClosure selected_callback_;
  KeyCallback key_callback_;
  const bool is_active_tab_;
  bool selected_ = false;
};

BEGIN_METADATA(CommandBarResultRow)
END_METADATA

CommandBarView::CommandBarView(CommandBarDisposition disposition,
                               std::u16string placeholder,
                               SuggestionsCallback suggestions_callback,
                               ExecuteCallback execute_callback,
                               base::RepeatingClosure close_callback,
                               base::OnceClosure destroyed_callback,
                               PrefService* prefs)
    : disposition_(disposition),
      suggestions_callback_(std::move(suggestions_callback)),
      execute_callback_(std::move(execute_callback)),
      close_callback_(std::move(close_callback)),
      destroyed_callback_(std::move(destroyed_callback)) {
  CHECK(suggestions_callback_);
  CHECK(execute_callback_);

  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      visual_style::kCommandBarVerticalSpacing));
  // The central appearance package owns this full-size surface. Child rows
  // remain independent so selection/hover cards stay readable in glass mode.
  SetBackground(nullptr);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);

  auto input_shell = std::make_unique<views::View>();
  input_shell->SetPreferredSize(
      gfx::Size(visual_style::kCommandBarContentWidth,
                visual_style::kCommandBarInputHeight));
  input_shell->SetBackground(views::CreateRoundedRectBackground(
      visual_style::kRaisedSurface, visual_style::kControlCornerRadius));
  input_shell->SetBorder(views::CreateRoundedRectBorder(
      visual_style::kControlBorderThickness, visual_style::kControlCornerRadius,
      visual_style::kAccent));
  auto* input_layout =
      input_shell->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, visual_style::kCommandBarInputHorizontalInset),
          visual_style::kCommandBarInputSpacing));
  input_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* search_icon = input_shell->AddChildView(
      std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
          vector_icons::kSearchIcon, visual_style::kMutedText,
          visual_style::kCommandBarInputIconSize)));
  search_icon->SetPreferredSize(
      gfx::Size(visual_style::kCommandBarResultIconBoxSize,
                visual_style::kCommandBarResultIconBoxSize));
  search_icon->SetImageSize(gfx::Size(visual_style::kCommandBarInputIconSize,
                                      visual_style::kCommandBarInputIconSize));
  search_icon->GetViewAccessibility().SetIsIgnored(true);

  textfield_ = input_shell->AddChildView(std::make_unique<views::Textfield>());
  textfield_->SetController(this);
  textfield_->SetPlaceholderText(placeholder);
  textfield_->SetAccessibleName(placeholder);
  textfield_->SetBorder(nullptr);
  textfield_->SetBackgroundColor(visual_style::kRaisedSurface);
  textfield_->SetTextColorId(visual_style::kText);
  textfield_->SetPlaceholderTextColorId(visual_style::kMutedText);
  textfield_->RemoveHoverEffect();
  input_layout->SetFlexForView(textfield_, 1);
  AddChildView(std::move(input_shell));

  auto results_view = std::make_unique<views::View>();
  results_view->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(visual_style::kCommandBarResultListVerticalInset, 0),
      visual_style::kCommandBarResultSpacing));
  results_view->GetViewAccessibility().SetRole(ax::mojom::Role::kListBox);
  results_view_ = AddChildView(std::move(results_view));

  const ui::AXPlatformNodeId textfield_id =
      textfield_->GetViewAccessibility().GetUniqueId();
  const ui::AXPlatformNodeId results_id =
      results_view_->GetViewAccessibility().GetUniqueId();
  results_view_->GetViewAccessibility().SetPopupForId(textfield_id);
  textfield_->GetViewAccessibility().SetControlIds({results_id});
  textfield_->GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kListbox);
  textfield_->GetViewAccessibility().SetIsExpanded();

  appearance_signal_source_ =
      std::make_unique<appearance::AppearanceRuntimeSignalSource>(
          prefs, base::BindRepeating(&CommandBarView::OnAppearanceChanged,
                                     weak_ptr_factory_.GetWeakPtr()));
  OnAppearanceChanged(appearance_signal_source_->policy());
}

CommandBarView::~CommandBarView() {
  if (textfield_) {
    textfield_->SetController(nullptr);
  }
  if (destroyed_callback_) {
    std::move(destroyed_callback_).Run();
  }
}

void CommandBarView::OnAppearanceChanged(
    const appearance::GlassPolicy& policy) {
  const appearance::SurfaceAppearance surface =
      appearance::AppearanceResolver::Resolve(
          appearance::SurfaceRole::kCommandBar, policy);
  views::ClientView* client_view =
      GetWidget() ? GetWidget()->client_view() : nullptr;
  if (!client_view) {
    appearance::ApplySurfaceAppearance(this, surface);
    return;
  }
  appearance::ClearSurfaceBackgroundAppearance(this);
  appearance::ApplySurfaceBackgroundAppearance(client_view, surface);
}

void CommandBarView::SetInitialQuery(std::u16string query,
                                     bool prefer_input_fallback) {
  textfield_->SetText(query);
  RebuildSuggestions(prefer_input_fallback);
  textfield_->SelectAll(/*reversed=*/false);
}

void CommandBarView::FocusInput() {
  textfield_->RequestFocus();
}

void CommandBarView::ReapplyAppearance() {
  OnAppearanceChanged(appearance_signal_source_->policy());
}

void CommandBarView::RefreshSuggestions() {
  std::optional<CommandBarSuggestion> selected_suggestion;
  bool selected_row_had_focus = false;
  if (selected_index_.has_value() && *selected_index_ < suggestions_.size() &&
      *selected_index_ < rows_.size()) {
    selected_suggestion = suggestions_[*selected_index_];
    selected_row_had_focus = rows_[*selected_index_]->HasFocus();
  }
  RebuildSuggestions(/*prefer_input_fallback=*/false);
  if (!selected_suggestion.has_value()) {
    return;
  }

  const auto selected = std::ranges::find_if(
      suggestions_, [&selected_suggestion](const CommandBarSuggestion& item) {
        return HasSameSuggestionIdentity(*selected_suggestion, item);
      });
  if (selected != suggestions_.end()) {
    SelectIndex(static_cast<size_t>(selected - suggestions_.begin()),
                selected_row_had_focus);
  } else if (selected_row_had_focus) {
    textfield_->RequestFocus();
  }
}

views::View* CommandBarView::row_for_testing(size_t index) const {
  return index < rows_.size() ? rows_[index].get() : nullptr;
}

bool CommandBarView::row_selected_for_testing(size_t index) const {
  return index < rows_.size() && rows_[index]->selected_for_testing();
}

bool CommandBarView::HandleKeyEventForTesting(const ui::KeyEvent& event) {
  return HandleKeyEvent(textfield_, event);
}

bool CommandBarView::HandleResultKeyEventForTesting(size_t index,
                                                    const ui::KeyEvent& event) {
  return HandleResultKeyEvent(index, event);
}

void CommandBarView::ContentsChanged(views::Textfield* sender,
                                     const std::u16string& new_contents) {
  CHECK_EQ(sender, textfield_);
  RebuildSuggestions(/*prefer_input_fallback=*/false);
}

bool CommandBarView::HandleKeyEvent(views::Textfield* sender,
                                    const ui::KeyEvent& key_event) {
  if (sender != textfield_ || key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  switch (key_event.key_code()) {
    case ui::VKEY_DOWN:
      return MoveSelection(/*delta=*/1, /*request_focus=*/false);
    case ui::VKEY_UP:
      return MoveSelection(/*delta=*/-1, /*request_focus=*/false);
    case ui::VKEY_TAB:
      if (!HasOnlyTabTraversalModifiers(key_event)) {
        return false;
      }
      // Keep native focus in the text field so typing can continue after
      // cycling. SelectIndex() publishes the highlighted option as the
      // textfield's active descendant for assistive technology.
      return MoveSelection(key_event.IsShiftDown() ? -1 : 1,
                           /*request_focus=*/false);
    case ui::VKEY_RETURN:
      return AcceptSelection();
    case ui::VKEY_ESCAPE:
      CloseCommandBar();
      return true;
    default:
      return false;
  }
}

void CommandBarView::OnBeforePaste(
    views::Textfield* sender,
    base::OnceCallback<void(std::optional<std::u16string>)> callback) {
  CHECK_EQ(sender, textfield_);
  ui::Clipboard::GetForCurrentThread()->ReadText(
      ui::ClipboardBuffer::kCopyPaste, /*data_dst=*/std::nullopt,
      base::BindOnce(
          [](base::OnceCallback<void(std::optional<std::u16string>)> callback,
             std::u16string text) {
            std::move(callback).Run(omnibox::SanitizeTextForPaste(text));
          },
          std::move(callback)));
}

views::View::DropCallback CommandBarView::CreateDropCallback(
    const ui::DropTargetEvent& event) {
  if (textfield_->HasTextBeingDragged()) {
    return base::NullCallback();
  }
  return base::BindOnce(&CommandBarView::PerformDrop,
                        weak_ptr_factory_.GetWeakPtr());
}

void CommandBarView::RebuildSuggestions(bool prefer_input_fallback) {
  suggestions_ = suggestions_callback_.Run(textfield_->GetText());
  if (suggestions_.size() > kMaximumSuggestionCount) {
    suggestions_.resize(kMaximumSuggestionCount);
  }
  // Clear the relation before destroying its previous target. Besides keeping
  // the accessibility tree coherent during replacement, resetting the index
  // prevents SelectIndex() from applying old selection state to a new row.
  textfield_->GetViewAccessibility().ClearActiveDescendant();
  selected_index_.reset();
  rows_.clear();
  results_view_->RemoveAllChildViews();

  for (size_t index = 0; index < suggestions_.size(); ++index) {
    auto row = std::make_unique<CommandBarResultRow>(
        suggestions_[index],
        base::BindRepeating(
            [](base::WeakPtr<CommandBarView> command_bar, size_t row_index,
               const ui::Event&) {
              if (!command_bar) {
                return;
              }
              command_bar->SelectIndex(row_index, /*request_focus=*/false);
              // Executing a result may activate another window or create a
              // native tab, either of which can synchronously destroy this
              // bubble. Run after Button::NotifyClick() has unwound so Cocoa
              // never continues dispatching through a deleted result row.
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](base::WeakPtr<CommandBarView> deferred_bar) {
                        if (deferred_bar) {
                          deferred_bar->AcceptSelection();
                        }
                      },
                      command_bar));
            },
            weak_ptr_factory_.GetWeakPtr(), index),
        base::BindRepeating(&CommandBarView::OnResultHovered,
                            base::Unretained(this), index),
        base::BindRepeating(&CommandBarView::HandleResultKeyEvent,
                            base::Unretained(this), index));
    row->GetViewAccessibility().SetPosInSet(static_cast<int>(index + 1));
    row->GetViewAccessibility().SetSetSize(
        static_cast<int>(suggestions_.size()));
    rows_.push_back(results_view_->AddChildView(std::move(row)));
  }

  std::optional<size_t> selection;
  if (!suggestions_.empty()) {
    selection = 0u;
    if (prefer_input_fallback) {
      const auto it = std::ranges::find(
          suggestions_, CommandBarSuggestionKind::kInputFallback,
          &CommandBarSuggestion::kind);
      if (it != suggestions_.end()) {
        selection = static_cast<size_t>(it - suggestions_.begin());
      }
    }
  }
  SelectIndex(selection, /*request_focus=*/false);
  results_view_->InvalidateLayout();
  InvalidateLayout();
}

void CommandBarView::SelectIndex(std::optional<size_t> index,
                                 bool request_focus) {
  if (index.has_value() && *index >= rows_.size()) {
    index.reset();
  }
  if (selected_index_.has_value() && *selected_index_ < rows_.size()) {
    rows_[*selected_index_]->SetCommandBarSelected(false);
  }

  selected_index_ = index;
  if (!selected_index_.has_value()) {
    textfield_->GetViewAccessibility().ClearActiveDescendant();
    return;
  }

  CommandBarResultRow* row = rows_[*selected_index_];
  row->SetCommandBarSelected(true);
  row->ScrollViewToVisible();
  textfield_->GetViewAccessibility().SetActiveDescendant(*row);
  if (request_focus) {
    row->RequestFocus();
  }
}

bool CommandBarView::MoveSelection(int delta, bool request_focus) {
  if (rows_.empty()) {
    return false;
  }
  const int row_count = static_cast<int>(rows_.size());
  int selected = selected_index_.has_value()
                     ? static_cast<int>(*selected_index_)
                     : (delta > 0 ? -1 : 0);
  selected = (selected + delta + row_count) % row_count;
  SelectIndex(static_cast<size_t>(selected), request_focus);
  return true;
}

bool CommandBarView::AcceptSelection() {
  if (!selected_index_.has_value() || *selected_index_ >= suggestions_.size()) {
    return false;
  }
  const CommandBarSuggestion suggestion = suggestions_[*selected_index_];
  const std::u16string input(textfield_->GetText());
  const base::WeakPtr<CommandBarView> weak_this =
      weak_ptr_factory_.GetWeakPtr();
  // The callback can synchronously close the bubble. Copy it before Run() so
  // its bind state survives destruction of this View during the invocation.
  const ExecuteCallback execute_callback = execute_callback_;
  if (!execute_callback.Run(suggestion, input)) {
    return false;
  }
  // Cross-window activation and focus-changing browser commands can close and
  // destroy the bubble synchronously inside the execution callback.
  if (weak_this) {
    weak_this->CloseCommandBar();
  }
  return true;
}

void CommandBarView::ScheduleAcceptSelection() {
  // A focused row receives its key event on the row itself. Defer execution so
  // an activating suggestion may close the command bar without deleting that
  // row while Views is still dispatching through CommandBarResultRow.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<CommandBarView> command_bar) {
                       if (command_bar) {
                         command_bar->AcceptSelection();
                       }
                     },
                     weak_ptr_factory_.GetWeakPtr()));
}

bool CommandBarView::HandleResultKeyEvent(size_t index,
                                          const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  switch (event.key_code()) {
    case ui::VKEY_DOWN:
      SelectIndex(index, /*request_focus=*/false);
      return MoveSelection(/*delta=*/1, /*request_focus=*/true);
    case ui::VKEY_UP:
      SelectIndex(index, /*request_focus=*/false);
      return MoveSelection(/*delta=*/-1, /*request_focus=*/true);
    case ui::VKEY_TAB:
      if (!HasOnlyTabTraversalModifiers(event)) {
        return false;
      }
      SelectIndex(index, /*request_focus=*/false);
      return MoveSelection(event.IsShiftDown() ? -1 : 1,
                           /*request_focus=*/true);
    case ui::VKEY_RETURN:
      SelectIndex(index, /*request_focus=*/false);
      ScheduleAcceptSelection();
      return true;
    case ui::VKEY_ESCAPE:
      CloseCommandBar();
      return true;
    default:
      return false;
  }
}

void CommandBarView::OnResultHovered(size_t index) {
  SelectIndex(index, /*request_focus=*/false);
}

void CommandBarView::PerformDrop(
    const ui::DropTargetEvent& event,
    ui::mojom::DragOperation& output_drag_op,
    std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
  std::u16string text;
  const std::vector<ui::ClipboardUrlInfo> url_infos =
      event.data().GetURLs(ui::FilenameToURLPolicy::CONVERT_FILENAMES);
  if (!url_infos.empty()) {
    text = base::UTF8ToUTF16(url_infos.front().url.spec());
  } else if (const std::optional<std::u16string> dropped_text =
                 event.data().GetString()) {
    text = base::CollapseWhitespace(*dropped_text, true);
  } else {
    output_drag_op = ui::mojom::DragOperation::kNone;
    return;
  }

  textfield_->SetText(omnibox::StripJavascriptSchemas(text));
  RebuildSuggestions(/*prefer_input_fallback=*/false);
  textfield_->SelectAll(/*reversed=*/false);
  output_drag_op = ui::mojom::DragOperation::kCopy;
}

void CommandBarView::CloseCommandBar() {
  // Keep the full-window scrim and panel animation in lockstep. Copy the
  // callback because it may synchronously destroy this view.
  const base::RepeatingClosure close_callback = close_callback_;
  if (close_callback) {
    close_callback.Run();
    return;
  }
  if (GetWidget()) {
    GetWidget()->Close();
  }
}

}  // namespace ahoi
