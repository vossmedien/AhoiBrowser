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

using RuntimeTabCallback =
    base::RepeatingCallback<void(base::WeakPtr<tabs::TabInterface>)>;
using RuntimeTabDragStateCallback =
    base::RepeatingCallback<void(std::optional<int>)>;
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
    ui::ImageModel favicon,
    std::vector<gfx::ImageSkia> drag_thumbnails,
    std::optional<tabs::TabAlert> media_alert,
    std::u16string status_text,
    bool active,
    bool sleeping,
    RuntimeTabCallback activate_callback,
    RuntimeTabCallback close_callback,
    RuntimeTabDragStateCallback drag_state_callback,
    CanDropOnRuntimeTabCallback can_drop_callback,
    DropOnRuntimeTabCallback drop_callback,
    views::ContextMenuController* context_menu_controller);

base::WeakPtr<tabs::TabInterface> GetOpenTabForView(views::View* view);

std::unique_ptr<views::View> CreateOpenTabSplitRowView(
    std::vector<std::unique_ptr<views::View>> tabs,
    split_tabs::SplitTabVisualData visual_data);

using DropSavedNodeToTemporaryCallback =
    base::RepeatingCallback<bool(const base::Uuid&)>;
std::unique_ptr<views::View> CreateOpenTabsDropTargetView(
    DropSavedNodeToTemporaryCallback callback);
void SetOpenTabsDropTargetAcceptingSavedTab(views::View* view, bool accepting);

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
