// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_title_label.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/i18n/rtl.h"
#include "base/numerics/safe_conversions.h"
#include "chrome/grit/generated_resources.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/skia_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"

namespace ahoi::sidebar {

namespace {

constexpr int kLeadingPadding = 8;
// One aligned slot for folders (including a small custom emblem) and pages.
// Expansion is in the folder silhouette; no empty caret column remains.
constexpr int kIconSlotSize = 24;
constexpr int kIconTitleSpacing = 8;
constexpr int kTrailingActionSize = 24;

}  // namespace

bool SidebarTreeRowView::HandleAccessibleAction(
    const ui::AXActionData& action_data) {
  if (is_bound()) {
    if (action_data.action == ax::mojom::Action::kFocus) {
      return owner_->OnRowAccessibilityFocused(this);
    }
    if (action_data.action == ax::mojom::Action::kDoDefault) {
      return owner_->OnRowAccessibilityActivated(this);
    }
  }
  return views::View::HandleAccessibleAction(action_data);
}

bool SidebarTreeRowView::HandleKeyEvent(views::Textfield* sender,
                                        const ui::KeyEvent& key_event) {
  if (sender != editor_ || key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    owner_->CommitRename(node_id_, std::u16string(editor_->GetText()));
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    owner_->CancelRename(node_id_);
    return true;
  }
  return false;
}

gfx::Rect SidebarTreeRowView::DisclosureBounds() const {
  return gfx::Rect();
}

gfx::Rect SidebarTreeRowView::IconBounds() const {
  const int x =
      kLeadingPadding + base::saturated_cast<int>(depth_) * kIndentWidth;
  return gfx::Rect(x, std::max(0, (height() - kIconSlotSize) / 2),
                   kIconSlotSize, kIconSlotSize);
}

gfx::Rect SidebarTreeRowView::TitleBounds() const {
  constexpr int kTitleTrailingGap = 7;
  const int x = IconBounds().right() + kIconTitleSpacing;
  int title_end = media_indicator_.IsEmpty() ? TrailingActionBounds().x()
                                             : MediaIndicatorBounds().x();
  title_end -= kTitleTrailingGap;
  // A split-drop preview labels the destination in the logical leading half.
  // The Label already tail-elides, so the trailing half stays visually clear
  // for the incoming pane preview in both LTR and RTL layouts.
  if (split_drop_target_) {
    title_end = std::min(title_end, width() / 2 - kTitleTrailingGap);
  }
  return gfx::Rect(x, 0, std::max(0, title_end - x), height());
}

gfx::Rect SidebarTreeRowView::MediaIndicatorBounds() const {
  const gfx::Rect action = TrailingActionBounds();
  gfx::Rect bounds(std::max(0, action.x() - kTrailingActionSize - 2),
                   action.y(), kTrailingActionSize, action.height());
  bounds.Intersect(GetLocalBounds());
  return bounds;
}

gfx::Rect SidebarTreeRowView::TrailingActionBounds() const {
  gfx::Rect bounds(std::max(0, width() - kTrailingActionSize - 4),
                   std::max(0, (height() - kTrailingActionSize) / 2),
                   kTrailingActionSize,
                   std::min(kTrailingActionSize, std::max(0, height())));
  bounds.Intersect(GetLocalBounds());
  return bounds;
}

bool SidebarTreeRowView::ShouldShowTrailingAction() const {
  // Selection/focus describes browser state, not pointer intent. Keep both the
  // loaded-tab power action and the unloaded saved-page remove action hidden
  // until the pointer is genuinely over this row, matching temporary tabs and
  // preventing a persistent close affordance from crowding the active title.
  return !split_drop_target_ && !is_folder() && hovered_;
}

bool SidebarTreeRowView::ShouldPaintTrailingState() const {
  return !split_drop_target_;
}

bool SidebarTreeRowView::IsTrailingActionAt(const gfx::Point& point) const {
  return ShouldShowTrailingAction() &&
         GetMirroredRect(TrailingActionBounds()).Contains(point);
}

void SidebarTreeRowView::UpdateAccessibility() {
  auto& accessibility = GetViewAccessibility();
  accessibility.SetIsInvisible(false);
  std::u16string accessible_name =
      split_drop_target_ ? split_with_prefix_ + u" " + title_ : title_;
  if (sleeping_) {
    accessible_name += u" — ";
    accessible_name += l10n_util::GetStringUTF16(IDS_AHOI_TAB_SLEEPING_TOOLTIP);
  }
  if (!status_text_.empty()) {
    accessible_name += u" — ";
    accessible_name += status_text_;
  }
  accessibility.SetName(accessible_name);
  std::u16string tooltip;
  if (sleeping_) {
    tooltip = l10n_util::GetStringUTF16(IDS_AHOI_TAB_SLEEPING_TOOLTIP);
  }
  if (!status_text_.empty()) {
    if (!tooltip.empty()) {
      tooltip += u" — ";
    }
    tooltip += status_text_;
  }
  SetTooltipText(tooltip);
  accessibility.SetHierarchicalLevel(base::saturated_cast<int>(depth_ + 1));
  accessibility.SetPosInSet(base::saturated_cast<int>(position_in_parent_));
  accessibility.SetSetSize(base::saturated_cast<int>(sibling_count_));
  accessibility.SetIsSelected(selected_);
  accessibility.SetDefaultActionVerb(is_folder() && !folder_navigation_result_
                                         ? ax::mojom::DefaultActionVerb::kClick
                                         : ax::mojom::DefaultActionVerb::kOpen);
  accessibility.AddAction(ax::mojom::Action::kDoDefault);
  if (is_folder() && !folder_navigation_result_) {
    if (expanded_) {
      accessibility.SetIsExpanded();
    } else {
      accessibility.SetIsCollapsed();
    }
  } else {
    accessibility.RemoveExpandCollapseState();
  }
}

}  // namespace ahoi::sidebar
