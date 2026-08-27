// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RUNTIME_TAB_VIEWS_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RUNTIME_TAB_VIEWS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/uuid.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_alert.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image_skia.h"

namespace tabs {
class TabInterface;
}

namespace ui {
class OSExchangeData;
}

namespace views {
class ContextMenuController;
class View;
}  // namespace views

namespace ahoi::sidebar {

enum class OpenTabDropPosition {
  kBefore = 0,
  kSplit,
  kAfter,
};

// A pane can be detached without a second target row by dropping it on either
// outer edge of its own runtime segment. The center remains the reorder/split
// target and a non-split tab has nothing to detach.
bool CanDetachRuntimeSplitPaneOnSelfDrop(bool source_is_split,
                                         OpenTabDropPosition position);

// A composite segment keeps its durable saved-node identity when available;
// only genuinely temporary panes use Chromium's process-local tab handle.
void WriteOpenTabDragPayload(ui::OSExchangeData* data,
                             std::optional<base::Uuid> saved_node_id,
                             int runtime_tab_handle,
                             const std::u16string& fallback_title);

using RuntimeTabCallback =
    base::RepeatingCallback<void(base::WeakPtr<tabs::TabInterface>)>;
using RuntimeTabDragStateCallback =
    base::RepeatingCallback<void(std::optional<int>)>;
using SavedTabDragStateCallback =
    base::RepeatingCallback<void(std::optional<base::Uuid>)>;
using CanDropOnRuntimeTabCallback =
    base::RepeatingCallback<bool(std::optional<base::Uuid>,
                                 std::optional<int>,
                                 base::WeakPtr<tabs::TabInterface>,
                                 OpenTabDropPosition)>;
using DropOnRuntimeTabCallback =
    base::RepeatingCallback<bool(std::optional<base::Uuid>,
                                 std::optional<int>,
                                 base::WeakPtr<tabs::TabInterface>,
                                 OpenTabDropPosition)>;

ui::ImageModel GetLiveTabFavicon(tabs::TabInterface* tab);

std::unique_ptr<views::View> CreateOpenTabRowView(
    tabs::TabInterface* tab,
    std::optional<base::Uuid> saved_node_id,
    ui::ImageModel favicon,
    std::vector<gfx::ImageSkia> drag_thumbnails,
    std::optional<tabs::TabAlert> media_alert,
    std::u16string status_text,
    bool active,
    bool sleeping,
    RuntimeTabCallback activate_callback,
    RuntimeTabCallback close_callback,
    SavedTabDragStateCallback saved_drag_state_callback,
    RuntimeTabDragStateCallback drag_state_callback,
    CanDropOnRuntimeTabCallback can_drop_callback,
    DropOnRuntimeTabCallback drop_callback,
    views::ContextMenuController* context_menu_controller);

base::WeakPtr<tabs::TabInterface> GetOpenTabForView(views::View* view);
std::optional<base::Uuid> GetSavedNodeForOpenTabView(views::View* view);

std::unique_ptr<views::View> CreateOpenTabSplitRowView(
    std::vector<std::unique_ptr<views::View>> tabs,
    split_tabs::SplitTabVisualData visual_data);

using DropSavedNodeToTemporaryCallback =
    base::RepeatingCallback<bool(const base::Uuid&)>;
std::unique_ptr<views::View> CreateOpenTabsDropTargetView(
    DropSavedNodeToTemporaryCallback callback);
void SetOpenTabsDropTargetAcceptingSavedTab(views::View* view, bool accepting);
bool IsOpenTabsDropTargetAcceptingSavedTabForTesting(const views::View* view);
bool IsOpenTabsDropTargetHighlightedForTesting(const views::View* view);

using CreateGroupForSavedNodeCallback =
    base::RepeatingCallback<void(const base::Uuid&)>;
using CreateGroupForRuntimeTabCallback = base::RepeatingCallback<void(int)>;
std::unique_ptr<views::View> CreateNewGroupDropTargetView(
    CreateGroupForSavedNodeCallback node_callback,
    CreateGroupForRuntimeTabCallback runtime_tab_callback);
void SetNewGroupDropTargetVisible(views::View* view, bool visible);

// Creates the same native presentation without consulting ResourceBundle.
// Focused Views tests use this to keep visual regressions independent from the
// much larger Chrome resource pack.
std::unique_ptr<views::View> CreateNewGroupDropTargetViewForTesting(
    std::u16string label);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_RUNTIME_TAB_VIEWS_H_
