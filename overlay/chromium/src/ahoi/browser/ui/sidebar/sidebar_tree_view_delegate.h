// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_DELEGATE_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_DELEGATE_H_

#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "base/uuid.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/image/image_skia.h"

namespace views {
class View;
}

namespace ahoi::sidebar {

// Browser-facing actions deliberately remain outside the reusable tree view.
// The delegate is owned by the embedding BrowserView integration and must
// outlive the SidebarTreeView.
class SidebarTreeViewDelegate {
 public:
  virtual ~SidebarTreeViewDelegate() = default;

  virtual void ActivateSavedPage(const tab_tree::TreeNode&) {}
  virtual bool CanSplitSavedPages(const base::Uuid& source_node_id,
                                  const base::Uuid& target_node_id) const = 0;
  virtual bool SplitSavedPages(const base::Uuid& source_node_id,
                               const base::Uuid& target_node_id) = 0;
  virtual bool CanReorderSavedSplitPanes(
      const base::Uuid& source_node_id,
      const base::Uuid& target_node_id) const = 0;
  virtual bool ReorderSavedSplitPanes(const base::Uuid& source_node_id,
                                      const base::Uuid& target_node_id) = 0;
  // Returns the currently visible Chromium split collections in pane order.
  // The tree keeps owning one logical node per tab while presenting every
  // collection as a single segmented sidebar row.
  virtual std::vector<std::vector<base::Uuid>> GetSplitSavedPageGroups()
      const = 0;
  // Returns the Chromium-owned arrangement for a split group. A missing value
  // keeps embedders/tests on the legacy balanced horizontal presentation.
  virtual std::optional<split_tabs::SplitTabVisualData>
  GetSplitSavedPageVisualData(const std::vector<base::Uuid>&) const = 0;
  // Returns the complete saved-page unit that must follow a dragged pane.
  // Non-split pages return a one-element vector containing `source_node_id`.
  virtual std::vector<base::Uuid> GetMoveGroupNodeIds(
      const base::Uuid& source_node_id) const = 0;
  // A drop from a split segment onto an ordinary tree target extracts only
  // that pane. The durable move commits first; the implementation then
  // rewrites Chromium split membership without touching WebContents.
  virtual bool CanExtractSavedSplitPaneForDrop(
      const base::Uuid& source_node_id,
      const std::optional<base::Uuid>& target_node_id) const = 0;
  virtual void ExtractSavedSplitPaneAfterDrop(
      const base::Uuid& source_node_id) = 0;
  virtual bool CanSaveTemporaryTab(
      int,
      const SidebarTreeController::DropTarget&) = 0;
  virtual bool SaveTemporaryTab(int,
                                const SidebarTreeController::DropTarget&) = 0;
  virtual bool CanSaveAndSplitTemporaryTab(int, const base::Uuid&) const = 0;
  virtual bool SaveAndSplitTemporaryTab(int, const base::Uuid&) = 0;
  virtual bool CanReorderTemporarySplitPane(int, const base::Uuid&) const = 0;
  virtual bool ReorderTemporarySplitPane(int, const base::Uuid&) = 0;
  virtual bool IsSavedPageRunning(const base::Uuid&) const = 0;
  // True when Chromium replaced this saved tab's WebContents with a
  // discarded/sleeping instance. Kept separate from `running` so a sleeping
  // tab remains a first-class saved page in the tree.
  virtual bool IsSavedPageSleeping(const base::Uuid&) const = 0;
  virtual ui::ImageModel GetSavedPageIcon(const tab_tree::TreeNode&) = 0;
  virtual ui::ImageModel GetSavedPageMediaIndicator(
      const tab_tree::TreeNode&) const = 0;
  // Localized Chromium-owned description for the visible media/capture
  // indicator. Custom-painted rows expose this alongside the title.
  virtual std::u16string GetSavedPageStatusText(
      const tab_tree::TreeNode&) const = 0;
  virtual std::vector<gfx::ImageSkia> GetSavedPageDragThumbnails(
      const base::Uuid&) const = 0;
  // A running saved page is closed but retained. A closed saved page is moved
  // to Trash, matching the stateful trailing action painted by the row.
  virtual void PerformSavedPageTrailingAction(const base::Uuid&) {}
  virtual void OnFolderHoverChanged(const base::Uuid&,
                                    views::View* anchor,
                                    bool hovered) {}
  virtual void OnSidebarDragStateChanged(
      std::optional<base::Uuid> dragged_node_id) {}
  virtual void OnTemporaryTabDragStateChanged(
      std::optional<int> runtime_tab_handle) {}
  // A missing node id represents the active workspace root. This keeps root
  // group creation reachable from an otherwise empty tree without inventing
  // a synthetic persistent node.
  virtual void ShowNodeContextMenu(std::optional<base::Uuid>,
                                   const gfx::Point&,
                                   ui::mojom::MenuSourceType) {}
  virtual void OnMutationFailed(tab_tree::TabTreeStore::Result) {}
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_DELEGATE_H_
