// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_view_test_support.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace ahoi::sidebar {

tab_tree::Workspace MakeWorkspace() {
  tab_tree::Workspace workspace;
  workspace.id = base::Uuid::GenerateRandomV4();
  workspace.name = u"Development";
  workspace.icon = u"folder";
  workspace.sort_key = "a";
  workspace.created_at = base::Time::UnixEpoch() + base::Seconds(1);
  workspace.modified_at = workspace.created_at;
  return workspace;
}

tab_tree::TreeNode MakeNode(const tab_tree::Workspace& workspace,
                            std::optional<base::Uuid> parent_id,
                            tab_tree::TreeNodeType type,
                            std::u16string title,
                            std::string sort_key) {
  tab_tree::TreeNode node;
  node.id = base::Uuid::GenerateRandomV4();
  node.workspace_id = workspace.id;
  node.parent_id = parent_id;
  node.type = type;
  node.title = std::move(title);
  node.sort_key = std::move(sort_key);
  node.created_at = base::Time::UnixEpoch() + base::Seconds(1);
  node.modified_at = node.created_at;
  if (type == tab_tree::TreeNodeType::kSavedPage) {
    node.url = GURL("https://example.test/");
  }
  return node;
}

RecordingDelegate::RecordingDelegate() = default;

RecordingDelegate::~RecordingDelegate() = default;

void RecordingDelegate::ActivateSavedPage(const tab_tree::TreeNode& node) {
  activated_node = node.id;
}

bool RecordingDelegate::CanSplitSavedPages(const base::Uuid&,
                                           const base::Uuid&) const {
  return can_split;
}

bool RecordingDelegate::SplitSavedPages(const base::Uuid& source,
                                        const base::Uuid& target) {
  split_requests.emplace_back(source, target);
  return split_succeeds;
}

bool RecordingDelegate::CanReorderSavedSplitPanes(const base::Uuid&,
                                                  const base::Uuid&) const {
  return can_reorder_split;
}

bool RecordingDelegate::ReorderSavedSplitPanes(const base::Uuid& source,
                                               const base::Uuid& target) {
  reorder_split_requests.emplace_back(source, target);
  return reorder_split_succeeds;
}

std::vector<std::vector<base::Uuid>>
RecordingDelegate::GetSplitSavedPageGroups() const {
  return split_groups;
}

std::optional<split_tabs::SplitTabVisualData>
RecordingDelegate::GetSplitSavedPageVisualData(
    const std::vector<base::Uuid>&) const {
  return split_visual_data;
}

std::vector<base::Uuid> RecordingDelegate::GetMoveGroupNodeIds(
    const base::Uuid& source_node_id) const {
  for (const std::vector<base::Uuid>& group : split_groups) {
    if (std::ranges::find(group, source_node_id) != group.end()) {
      return group;
    }
  }
  return {source_node_id};
}

bool RecordingDelegate::CanSaveTemporaryTab(
    int,
    const SidebarTreeController::DropTarget&) {
  return can_save_temporary;
}

bool RecordingDelegate::SaveTemporaryTab(
    int runtime_tab_handle,
    const SidebarTreeController::DropTarget& target) {
  saved_temporary_tabs.emplace_back(runtime_tab_handle, target);
  return save_temporary_succeeds;
}

bool RecordingDelegate::CanSaveAndSplitTemporaryTab(int,
                                                    const base::Uuid&) const {
  return can_split_temporary;
}

bool RecordingDelegate::SaveAndSplitTemporaryTab(int runtime_tab_handle,
                                                 const base::Uuid& target) {
  split_temporary_requests.emplace_back(runtime_tab_handle, target);
  return split_temporary_succeeds;
}

bool RecordingDelegate::CanReorderTemporarySplitPane(int,
                                                     const base::Uuid&) const {
  return can_reorder_temporary_split;
}

bool RecordingDelegate::ReorderTemporarySplitPane(int runtime_tab_handle,
                                                  const base::Uuid& target) {
  reorder_temporary_split_requests.emplace_back(runtime_tab_handle, target);
  return reorder_temporary_split_succeeds;
}

bool RecordingDelegate::IsSavedPageRunning(const base::Uuid&) const {
  return saved_page_running;
}

bool RecordingDelegate::IsSavedPageSleeping(const base::Uuid&) const {
  return false;
}

ui::ImageModel RecordingDelegate::GetSavedPageIcon(const tab_tree::TreeNode&) {
  return ui::ImageModel();
}

ui::ImageModel RecordingDelegate::GetSavedPageMediaIndicator(
    const tab_tree::TreeNode&) const {
  return ui::ImageModel();
}

std::u16string RecordingDelegate::GetSavedPageStatusText(
    const tab_tree::TreeNode&) const {
  return saved_page_status_text;
}

std::vector<gfx::ImageSkia> RecordingDelegate::GetSavedPageDragThumbnails(
    const base::Uuid&) const {
  return {};
}

void RecordingDelegate::OnMutationFailed(
    tab_tree::TabTreeStore::Result result) {
  last_error = result;
}

void RecordingDelegate::OnSidebarDragStateChanged(
    std::optional<base::Uuid> dragged_node_id) {
  drag_state = std::move(dragged_node_id);
}

SidebarTreeViewTest::SidebarTreeViewTest() = default;

SidebarTreeViewTest::~SidebarTreeViewTest() = default;

void SidebarTreeViewTest::SetUp() {
  ViewsTestBase::SetUp();
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  ASSERT_TRUE(store_.Initialize(
      temp_dir_.GetPath().AppendASCII("SidebarTreeView.sqlite")));
  controller_ = std::make_unique<SidebarTreeController>(&store_);
}

std::unique_ptr<SidebarTreeView> SidebarTreeViewTest::NewTreeView() {
  auto view = std::make_unique<SidebarTreeView>(
      controller_.get(), &delegate_, u"Tabs and bookmarks", u"Split with");
  view->SetBounds(0, 0, 240, 320);
  return view;
}

}  // namespace ahoi::sidebar
