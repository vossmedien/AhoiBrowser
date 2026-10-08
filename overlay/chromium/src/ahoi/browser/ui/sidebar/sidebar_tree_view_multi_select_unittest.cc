// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <vector>

#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_test_support.h"
#include "ui/events/event.h"

namespace ahoi::sidebar {
namespace {

class RuntimeSelectionDelegate : public RecordingDelegate {
 public:
  std::vector<base::Uuid> GetMultiSelectionRowOrder() const override {
    return order;
  }
  std::optional<base::Uuid> GetMultiSelectionActiveNode() const override {
    return active;
  }
  void OnMultiSelectionChanged() override { ++changes; }
  std::vector<base::Uuid> order;
  std::optional<base::Uuid> active;
  int changes = 0;
};

void Click(SidebarTreeView& view, const base::Uuid& node_id, int modifiers) {
  SidebarTreeRowView* row = view.GetMaterializedRowForTesting(node_id);
  ASSERT_NE(nullptr, row);
  const gfx::Point point(80, SidebarTreeRowView::kRowHeight / 2);
  const int flags = ui::EF_LEFT_MOUSE_BUTTON | modifiers;
  ui::MouseEvent press(ui::EventType::kMousePressed, point, point,
                       base::TimeTicks::Now(), flags, ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased, point, point,
                         base::TimeTicks::Now(), flags,
                         ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(row->OnMousePressed(press));
  row->OnMouseReleased(release);
}

TEST_F(SidebarTreeViewTest, CommandAndShiftClickBuildAMultiSelection) {
  tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode a = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage, u"A", "a");
  tab_tree::TreeNode b = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage, u"B", "b");
  tab_tree::TreeNode c = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage, u"C", "c");
  auto view = NewTreeView();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {a, b, c}));
  ASSERT_TRUE(controller_->SelectNode(a.id));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 144));

  // ⌘-click adds to the selected row and does not activate anything.
  Click(*view, c.id, ui::EF_COMMAND_DOWN);
  EXPECT_EQ((std::vector<base::Uuid>{a.id, c.id}), view->multi_selection());
  EXPECT_FALSE(delegate_.activated_node.has_value());

  // ⇧-click replaces it with the range from the anchor (c) to b.
  Click(*view, b.id, ui::EF_SHIFT_DOWN);
  EXPECT_EQ((std::vector<base::Uuid>{b.id, c.id}), view->multi_selection());

  // ⌘-click on a selected row removes it.
  Click(*view, c.id, ui::EF_COMMAND_DOWN);
  EXPECT_EQ((std::vector<base::Uuid>{b.id}), view->multi_selection());

  // A plain click clears the multi-selection and activates as before.
  Click(*view, a.id, 0);
  EXPECT_TRUE(view->multi_selection().empty());
  EXPECT_EQ(a.id, delegate_.activated_node);
}

TEST_F(SidebarTreeViewTest, EscapeClearsTheMultiSelection) {
  tab_tree::Workspace workspace = MakeWorkspace();
  tab_tree::TreeNode a = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage, u"A", "a");
  tab_tree::TreeNode b = MakeNode(
      workspace, std::nullopt, tab_tree::TreeNodeType::kSavedPage, u"B", "b");
  auto view = NewTreeView();
  auto& model = controller_->view_model();
  ASSERT_TRUE(model.ResetWorkspace(workspace.id));
  ASSERT_TRUE(model.ReplaceChildren(std::nullopt, {a, b}));
  ASSERT_TRUE(controller_->SelectNode(a.id));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 96));

  Click(*view, b.id, ui::EF_COMMAND_DOWN);
  ASSERT_EQ(2u, view->multi_selection().size());
  ui::KeyEvent escape(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE,
                      ui::EF_NONE);
  EXPECT_TRUE(view->OnKeyPressed(escape));
  EXPECT_TRUE(view->multi_selection().empty());
}

TEST_F(SidebarTreeViewTest, RuntimeSelectionUsesVisibleOrderAndSharedActiveNode) {
  const tab_tree::Workspace workspace = MakeWorkspace();
  const auto saved = MakeNode(workspace, std::nullopt,
                             tab_tree::TreeNodeType::kSavedPage, u"Saved", "a");
  const auto alpha = base::Uuid::GenerateRandomV4();
  const auto gamma = base::Uuid::GenerateRandomV4();
  RuntimeSelectionDelegate runtime;
  // Runtime identities need no materialized saved row; visible order wins.
  runtime.order = {saved.id, gamma, alpha};
  runtime.active = alpha;
  auto view = std::make_unique<SidebarTreeView>(
      controller_.get(), &runtime, u"Tabs", u"Split with");
  view->SetBounds(0, 0, 240, 320);
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(std::nullopt, {saved}));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 144));
  const gfx::Point point(80, 18);
  ui::MouseEvent command(ui::EventType::kMouseReleased, point, point,
                         base::TimeTicks::Now(),
                         ui::EF_LEFT_MOUSE_BUTTON | ui::EF_COMMAND_DOWN,
                         ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(view->HandleMultiSelectClick(gamma, command));
  EXPECT_EQ((std::vector<base::Uuid>{gamma, alpha}), view->multi_selection());
  EXPECT_FALSE(runtime.activated_node);
  // The saved-row caller uses the same order and the runtime anchor.
  Click(*view, saved.id, ui::EF_SHIFT_DOWN);
  EXPECT_EQ((std::vector<base::Uuid>{saved.id, gamma}), view->multi_selection());
  EXPECT_FALSE(runtime.activated_node);
  view->ClearMultiSelection();
  EXPECT_EQ(3, runtime.changes);
}

TEST_F(SidebarTreeViewTest, HiddenRuntimeRowsLoseSelectionWhenGroupCollapses) {
  const auto workspace = MakeWorkspace();
  const auto alpha = base::Uuid::GenerateRandomV4();
  const auto gamma = base::Uuid::GenerateRandomV4();
  RuntimeSelectionDelegate runtime;
  runtime.order = {alpha, gamma};
  runtime.active = alpha;
  auto view = std::make_unique<SidebarTreeView>(
      controller_.get(), &runtime, u"Tabs", u"Split with");
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ui::MouseEvent command(ui::EventType::kMouseReleased, gfx::Point(),
                         gfx::Point(), base::TimeTicks::Now(),
                         ui::EF_LEFT_MOUSE_BUTTON | ui::EF_COMMAND_DOWN,
                         ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(view->HandleMultiSelectClick(gamma, command));
  ASSERT_EQ((std::vector<base::Uuid>{alpha, gamma}), view->multi_selection());
  runtime.order = {alpha};
  view->PruneMultiSelectionToVisibleRows();
  EXPECT_FALSE(view->IsMultiSelected(gamma));
  EXPECT_EQ((std::vector<base::Uuid>{alpha}), view->multi_selection());
  runtime.order = {alpha, gamma};
  EXPECT_FALSE(view->IsMultiSelected(gamma));
  runtime.order.clear();
  view->PruneMultiSelectionToVisibleRows();
  EXPECT_FALSE(view->has_multi_selection());
}

TEST_F(SidebarTreeViewTest, AXSelectionDescribesTheRangeWithoutChangingActivePage) {
  const auto workspace = MakeWorkspace();
  const auto a = MakeNode(workspace, std::nullopt,
                         tab_tree::TreeNodeType::kSavedPage, u"A", "a");
  const auto b = MakeNode(workspace, std::nullopt,
                         tab_tree::TreeNodeType::kSavedPage, u"B", "b");
  const auto c = MakeNode(workspace, std::nullopt,
                         tab_tree::TreeNodeType::kSavedPage, u"C", "c");
  auto view = NewTreeView();
  ASSERT_TRUE(controller_->view_model().ResetWorkspace(workspace.id));
  ASSERT_TRUE(controller_->view_model().ReplaceChildren(std::nullopt, {a, b, c}));
  ASSERT_TRUE(controller_->SelectNode(a.id));
  view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 144));
  const auto ax_selected = [&](const base::Uuid& id) {
    ui::AXNodeData data;
    view->GetMaterializedRowForTesting(id)->GetViewAccessibility()
        .GetAccessibleNodeData(&data);
    return data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected);
  };
  Click(*view, c.id, ui::EF_COMMAND_DOWN);
  Click(*view, b.id, ui::EF_SHIFT_DOWN);
  EXPECT_FALSE(ax_selected(a.id));
  EXPECT_TRUE(ax_selected(b.id));
  EXPECT_TRUE(ax_selected(c.id));
  EXPECT_EQ(a.id, controller_->view_model().selected_node_id());
  EXPECT_FALSE(delegate_.activated_node);
  view->ClearMultiSelection();
  EXPECT_TRUE(ax_selected(a.id));
  EXPECT_FALSE(ax_selected(b.id));
  EXPECT_FALSE(ax_selected(c.id));
}

}  // namespace
}  // namespace ahoi::sidebar
