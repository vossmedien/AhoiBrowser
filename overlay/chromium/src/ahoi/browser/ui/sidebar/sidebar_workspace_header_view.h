// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_WORKSPACE_HEADER_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_WORKSPACE_HEADER_VIEW_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Button;
}

namespace ahoi::sidebar {

// Everything the header needs to decide what fits beside the workspace
// selector. Widths are in DIP.
struct WorkspaceHeaderMetrics {
  int available_width = 0;
  // Selector with the full workspace name, without the workspace dots.
  int selector_width = 0;
  // Extra width the workspace dots add to the selector (0 without dots).
  int indicators_width = 0;
  // Actions that always stay (search) and those that may move into the
  // selector's menu, which already offers them.
  int essential_actions = 0;
  int collapsible_actions = 0;
  int action_width = 0;
  int action_spacing = 0;
  // Gap between the selector and the first action.
  int selector_spacing = 0;
};

struct WorkspaceHeaderPlan {
  // The first `collapsed_actions` collapsible actions are hidden.
  int collapsed_actions = 0;
  bool show_indicators = true;
  // False only when even the most compact plan elides the name.
  bool name_fits = true;

  friend bool operator==(const WorkspaceHeaderPlan&,
                         const WorkspaceHeaderPlan&) = default;
};

// The workspace name wins over secondary chrome. In order, until the full
// name fits: the first collapsible action (floating sidebar) moves into
// the menu, the workspace dots hide, then the remaining collapsible
// actions move into the menu. Only then does the name elide.
WorkspaceHeaderPlan PlanWorkspaceHeader(const WorkspaceHeaderMetrics& m);

// Row with the workspace selector and the round header actions. The
// selector takes all width the plan leaves; actions sit at the trailing
// edge. Collapsed actions are hidden, never squeezed.
class SidebarWorkspaceHeaderView final : public views::View {
  METADATA_HEADER(SidebarWorkspaceHeaderView, views::View)

 public:
  SidebarWorkspaceHeaderView();
  SidebarWorkspaceHeaderView(const SidebarWorkspaceHeaderView&) = delete;
  SidebarWorkspaceHeaderView& operator=(const SidebarWorkspaceHeaderView&) =
      delete;
  ~SidebarWorkspaceHeaderView() override;

  // `host` is the child containing `selector`; every other child is an
  // action, laid out in child order.
  void SetSelector(views::View* host, views::Button* selector);
  // Actions collapse in the order they are marked.
  void MarkCollapsibleAction(views::View* action);

  const WorkspaceHeaderPlan& plan_for_testing() const { return plan_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  WorkspaceHeaderMetrics MeasureFor(int available_width) const;
  bool IsCollapsible(const views::View* view) const;

  raw_ptr<views::View> selector_host_ = nullptr;
  raw_ptr<views::Button> selector_ = nullptr;
  std::vector<raw_ptr<views::View>> collapsible_;
  WorkspaceHeaderPlan plan_;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_WORKSPACE_HEADER_VIEW_H_
