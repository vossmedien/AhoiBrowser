// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/command_bar_decorations.h"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <utility>

#include "ahoi/browser/ui/dialog_style.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"

namespace ahoi {

namespace {

// Keycaps are symbols, as on the Mac keyboard; only the actions are words.
constexpr char16_t kUpKeycap[] = u"↑";
constexpr char16_t kDownKeycap[] = u"↓";
constexpr char16_t kAcceptKeycap[] = u"↵";
constexpr char16_t kEscapeKeycap[] = u"esc";

gfx::FontList HintFont() {
  const gfx::FontList base;
  return base
      .DeriveWithSizeDelta(visual_style::kCommandBarResultOriginFontSize -
                           base.GetFontSize())
      .DeriveWithWeight(gfx::Font::Weight::NORMAL);
}

std::unique_ptr<views::Label> CreateHintLabel(std::u16string text) {
  auto label = std::make_unique<views::Label>(
      std::move(text), views::style::CONTEXT_LABEL,
      views::style::STYLE_SECONDARY);
  label->SetSubpixelRenderingEnabled(false);
  label->SetFontList(HintFont());
  label->SetEnabledColor(visual_style::kMutedText);
  return label;
}

// The same small key as the selected row's ↵ keycap.
std::unique_ptr<views::Label> CreateKeycap(std::u16string key) {
  auto keycap = CreateHintLabel(std::move(key));
  keycap->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  keycap->SetBorder(views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(
          visual_style::kControlBorderThickness,
          visual_style::kCommandBarKeycapCornerRadius, visual_style::kDivider),
      gfx::Insets::VH(0, visual_style::kCommandBarKeycapHorizontalPadding)));
  const gfx::Size preferred = keycap->GetPreferredSize();
  keycap->SetPreferredSize(
      gfx::Size(std::max(preferred.width(),
                         visual_style::kCommandBarAcceptHintWidth),
                visual_style::kCommandBarAcceptHintHeight));
  return keycap;
}

std::unique_ptr<views::View> CreateHorizontalBox(int spacing) {
  auto box = std::make_unique<views::View>();
  box->SetLayoutManager(std::make_unique<views::BoxLayout>(
                            views::BoxLayout::Orientation::kHorizontal,
                            gfx::Insets(), spacing))
      ->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
  return box;
}

// One hint: its keys side by side, then the action a little apart.
void AddHintGroup(views::View& row,
                  std::initializer_list<const char16_t*> keys,
                  int action_message_id) {
  auto* group = row.AddChildView(
      CreateHorizontalBox(visual_style::kCommandBarKeycapLabelGap));
  auto* keycaps = group->AddChildView(
      CreateHorizontalBox(visual_style::kCommandBarKeycapSpacing));
  for (const char16_t* key : keys) {
    keycaps->AddChildView(CreateKeycap(key));
  }
  group->AddChildView(
      CreateHintLabel(l10n_util::GetStringUTF16(action_message_id)));
}

bool HasKeyboardFocusWithin(const views::View* shell) {
  const views::FocusManager* focus_manager = shell->GetFocusManager();
  if (!focus_manager) {
    return false;
  }
  const views::View* focused = focus_manager->GetFocusedView();
  return focused && shell->Contains(focused) &&
         focus_manager->focus_change_reason() ==
             views::FocusManager::FocusChangeReason::kFocusTraversal;
}

}  // namespace

std::unique_ptr<views::View> CreateCommandBarKeyHints() {
  auto footer = std::make_unique<views::View>();
  footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  footer->AddChildView(std::make_unique<views::Separator>())
      ->SetColorId(visual_style::kDivider);

  auto* row = footer->AddChildView(std::make_unique<views::View>());
  row->SetPreferredSize(gfx::Size(visual_style::kCommandBarContentWidth,
                                  visual_style::kCommandBarFooterHeight));
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(visual_style::kCommandBarVerticalSpacing,
                        visual_style::kCommandBarResultHorizontalInset, 0,
                        visual_style::kCommandBarResultHorizontalInset),
      visual_style::kCommandBarFooterGroupSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  AddHintGroup(*row, {kUpKeycap, kDownKeycap},
               IDS_AHOI_COMMAND_BAR_HINT_SELECT);
  AddHintGroup(*row, {kAcceptKeycap}, IDS_AHOI_COMMAND_BAR_HINT_OPEN);
  AddHintGroup(*row, {kEscapeKeycap}, IDS_AHOI_COMMAND_BAR_HINT_CLOSE);

  footer->GetViewAccessibility().SetIsIgnored(true);
  return footer;
}

CommandBarInputFocusRing::CommandBarInputFocusRing(views::View* shell,
                                                   views::View* field)
    : shell_(shell) {
  CHECK(shell_);
  CHECK(field);
  CHECK(shell_->Contains(field));
  views::InstallRoundRectHighlightPathGenerator(
      shell_, gfx::Insets(), visual_style::kControlCornerRadius);
  views::FocusRing::Install(shell_);
  views::FocusRing* const ring = views::FocusRing::Get(shell_);
  ring->SetHasFocusPredicate(base::BindRepeating(&HasKeyboardFocusWithin));
  dialog_style::StyleFocusRing(*shell_);
  observation_.Observe(field);
}

CommandBarInputFocusRing::~CommandBarInputFocusRing() = default;

bool CommandBarInputFocusRing::IsShowingForTesting() const {
  const views::FocusRing* const ring =
      shell_ ? views::FocusRing::Get(shell_) : nullptr;
  return ring && ring->GetVisible();
}

void CommandBarInputFocusRing::OnViewFocused(views::View* observed_view) {
  Refresh();
}

void CommandBarInputFocusRing::OnViewBlurred(views::View* observed_view) {
  Refresh();
}

void CommandBarInputFocusRing::OnViewIsDeleting(views::View* observed_view) {
  observation_.Reset();
  shell_ = nullptr;
}

void CommandBarInputFocusRing::Refresh() {
  if (views::FocusRing* const ring =
          shell_ ? views::FocusRing::Get(shell_) : nullptr) {
    ring->Refresh();
  }
}

}  // namespace ahoi
