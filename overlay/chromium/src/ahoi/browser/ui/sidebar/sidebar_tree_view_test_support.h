// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_TEST_SUPPORT_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_TEST_SUPPORT_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "base/files/scoped_temp_dir.h"
#include "base/pickle.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace ahoi::sidebar {

tab_tree::Workspace MakeWorkspace();
tab_tree::TreeNode MakeNode(const tab_tree::Workspace& workspace,
                            std::optional<base::Uuid> parent_id,
                            tab_tree::TreeNodeType type,
                            std::u16string title,
                            std::string sort_key);

class RecordingDelegate : public SidebarTreeViewDelegate {
 public:
  RecordingDelegate();
  ~RecordingDelegate() override;

  void ActivateSavedPage(const tab_tree::TreeNode& node) override;
  bool CanSplitSavedPages(const base::Uuid&, const base::Uuid&) const override;
  bool SplitSavedPages(const base::Uuid& source,
                       const base::Uuid& target) override;
  bool CanReorderSavedSplitPanes(const base::Uuid&,
                                 const base::Uuid&) const override;
  bool ReorderSavedSplitPanes(const base::Uuid& source,
                              const base::Uuid& target) override;
  std::vector<std::vector<base::Uuid>> GetSplitSavedPageGroups() const override;
  std::optional<split_tabs::SplitTabVisualData> GetSplitSavedPageVisualData(
      const std::vector<base::Uuid>&) const override;
  std::vector<base::Uuid> GetMoveGroupNodeIds(
      const base::Uuid& source_node_id) const override;
  bool CanSaveTemporaryTab(int,
                           const SidebarTreeController::DropTarget&) override;
  bool SaveTemporaryTab(
      int runtime_tab_handle,
      const SidebarTreeController::DropTarget& target) override;
  bool CanSaveAndSplitTemporaryTab(int, const base::Uuid&) const override;
  bool SaveAndSplitTemporaryTab(int runtime_tab_handle,
                                const base::Uuid& target) override;
  bool CanReorderTemporarySplitPane(int, const base::Uuid&) const override;
  bool ReorderTemporarySplitPane(int runtime_tab_handle,
                                 const base::Uuid& target) override;
  bool IsSavedPageRunning(const base::Uuid&) const override;
  bool IsSavedPageSleeping(const base::Uuid&) const override;
  ui::ImageModel GetSavedPageIcon(const tab_tree::TreeNode&) override;
  ui::ImageModel GetSavedPageMediaIndicator(
      const tab_tree::TreeNode&) const override;
  std::u16string GetSavedPageStatusText(
      const tab_tree::TreeNode&) const override;
  std::vector<gfx::ImageSkia> GetSavedPageDragThumbnails(
      const base::Uuid&) const override;
  void OnMutationFailed(tab_tree::TabTreeStore::Result result) override;
  void OnSidebarDragStateChanged(
      std::optional<base::Uuid> dragged_node_id) override;

  bool can_split = false;
  bool split_succeeds = false;
  bool can_reorder_split = false;
  bool reorder_split_succeeds = false;
  bool can_save_temporary = false;
  bool save_temporary_succeeds = false;
  bool can_split_temporary = false;
  bool split_temporary_succeeds = false;
  bool can_reorder_temporary_split = false;
  bool reorder_temporary_split_succeeds = false;
  bool saved_page_running = false;
  std::u16string saved_page_status_text;
  std::optional<base::Uuid> activated_node;
  std::optional<tab_tree::TabTreeStore::Result> last_error;
  std::optional<base::Uuid> drag_state;
  std::vector<std::pair<base::Uuid, base::Uuid>> split_requests;
  std::vector<std::pair<base::Uuid, base::Uuid>> reorder_split_requests;
  std::vector<std::pair<int, SidebarTreeController::DropTarget>>
      saved_temporary_tabs;
  std::vector<std::pair<int, base::Uuid>> split_temporary_requests;
  std::vector<std::pair<int, base::Uuid>> reorder_temporary_split_requests;
  std::vector<std::vector<base::Uuid>> split_groups;
  std::optional<split_tabs::SplitTabVisualData> split_visual_data;
};

class SidebarTreeViewTest : public views::ViewsTestBase {
 public:
  SidebarTreeViewTest();
  ~SidebarTreeViewTest() override;

  void SetUp() override;

 protected:
  std::unique_ptr<SidebarTreeView> NewTreeView();

  base::ScopedTempDir temp_dir_;
  tab_tree::TabTreeStore store_;
  std::unique_ptr<SidebarTreeController> controller_;
  RecordingDelegate delegate_;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_TEST_SUPPORT_H_
