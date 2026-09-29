// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_workspace_header_view.h"

#include <algorithm>

#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/containers/adapters.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/button/button.h"

namespace ahoi::sidebar {

namespace {

int ActionsWidth(const WorkspaceHeaderMetrics& m, int visible_actions) {
  if (visible_actions <= 0) {
    return 0;
  }
  return visible_actions * m.action_width +
         (visible_actions - 1) * m.action_spacing + m.selector_spacing;
}

}  // namespace

WorkspaceHeaderPlan PlanWorkspaceHeader(const WorkspaceHeaderMetrics& m) {
  const int collapsible = std::max(0, m.collapsible_actions);
  std::vector<WorkspaceHeaderPlan> candidates = {{0, true, true}};
  if (collapsible > 0) {
    candidates.push_back({1, true, true});
  }
  candidates.push_back({std::min(1, collapsible), false, true});
  for (int collapsed = 2; collapsed <= collapsible; ++collapsed) {
    candidates.push_back({collapsed, false, true});
  }
  for (const WorkspaceHeaderPlan& candidate : candidates) {
    const int actions = std::max(0, m.essential_actions) + collapsible -
                        candidate.collapsed_actions;
    const int room = m.available_width - ActionsWidth(m, actions);
    const int needed =
        m.selector_width + (candidate.show_indicators ? m.indicators_width : 0);
    if (room >= needed) {
      return candidate;
    }
  }
  WorkspaceHeaderPlan compact = candidates.back();
  compact.name_fits = false;
  return compact;
}

SidebarWorkspaceHeaderView::SidebarWorkspaceHeaderView() = default;

SidebarWorkspaceHeaderView::~SidebarWorkspaceHeaderView() = default;

void SidebarWorkspaceHeaderView::SetSelector(views::View* host,
                                             views::Button* selector) {
  selector_host_ = host;
  selector_ = selector;
  InvalidateLayout();
}

bool SidebarWorkspaceHeaderView::IsCollapsible(const views::View* view) const {
  return std::ranges::any_of(collapsible_, [view](const auto& action) {
    return action.get() == view;
  });
}

void SidebarWorkspaceHeaderView::MarkCollapsibleAction(views::View* action) {
  if (action && !IsCollapsible(action)) {
    collapsible_.push_back(action);
    InvalidateLayout();
  }
}

WorkspaceHeaderMetrics SidebarWorkspaceHeaderView::MeasureFor(
    int available_width) const {
  WorkspaceHeaderMetrics m;
  m.available_width = available_width;
  if (selector_) {
    m.selector_width = GetWorkspaceSelectorPreferredWidth(
        selector_, /*with_indicators=*/false);
    m.indicators_width =
        GetWorkspaceSelectorPreferredWidth(selector_,
                                           /*with_indicators=*/true) -
        m.selector_width;
  }
  for (const views::View* child : children()) {
    if (child == selector_host_) {
      continue;
    }
    if (IsCollapsible(child)) {
      ++m.collapsible_actions;
    } else {
      ++m.essential_actions;
    }
  }
  m.action_width = visual_style::kSidebarHeaderActionSize;
  m.action_spacing = visual_style::kSidebarHeaderActionSpacing;
  m.selector_spacing = visual_style::kSidebarFooterSpacing;
  return m;
}

gfx::Size SidebarWorkspaceHeaderView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // The sidebar gives the header its full content width; the plan decides
  // what fits in it. Report the uncollapsed width as the ideal.
  const WorkspaceHeaderMetrics m = MeasureFor(0);
  const int width =
      m.selector_width + m.indicators_width +
      ActionsWidth(m, m.essential_actions + m.collapsible_actions);
  return gfx::Size(width, std::max(visual_style::kSidebarActionCellHeight,
                                   visual_style::kSidebarHeaderActionSize));
}

void SidebarWorkspaceHeaderView::Layout(PassKey) {
  plan_ = PlanWorkspaceHeader(MeasureFor(width()));
  if (selector_) {
    SetWorkspaceSelectorFit(selector_, plan_.show_indicators,
                            !plan_.name_fits);
  }
  for (size_t index = 0; index < collapsible_.size(); ++index) {
    collapsible_[index]->SetVisible(static_cast<int>(index) >=
                                    plan_.collapsed_actions);
  }

  // Actions from the trailing edge, in child order.
  int actions_left = width();
  bool any_action = false;
  for (views::View* child : base::Reversed(children())) {
    if (child == selector_host_ || !child->GetVisible()) {
      continue;
    }
    const gfx::Size size = child->GetPreferredSize();
    if (any_action) {
      actions_left -= visual_style::kSidebarHeaderActionSpacing;
    }
    actions_left -= size.width();
    child->SetBoundsRect(GetMirroredRect(
        gfx::Rect(actions_left, (height() - size.height()) / 2, size.width(),
                  size.height())));
    any_action = true;
  }
  if (!selector_host_) {
    return;
  }
  const int selector_width = std::max(
      0, actions_left - (any_action ? visual_style::kSidebarFooterSpacing : 0));
  const int selector_height =
      std::min(height(), selector_host_->GetPreferredSize().height());
  selector_host_->SetBoundsRect(GetMirroredRect(gfx::Rect(
      0, (height() - selector_height) / 2, selector_width, selector_height)));
}

BEGIN_METADATA(SidebarWorkspaceHeaderView)
END_METADATA

}  // namespace ahoi::sidebar
