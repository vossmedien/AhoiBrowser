// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_H_
#define AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_H_

#include <cstddef>
#include <memory>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"
#include "base/memory/raw_ptr.h"
#include "base/uuid.h"

class Browser;

namespace tabs {
class TabInterface;
}

namespace views {
class View;
}

namespace ahoi {
class ModalOverlayController;
}

namespace ahoi::sidebar {

struct BrowserSidebarSplitDropSource {
  bool valid = false;
  raw_ptr<tabs::TabInterface> tab = nullptr;
};

// Creates the profile-backed Ahoi organization surface for a normal browser
// window. Unsupported and private profiles deliberately return nullptr.
std::unique_ptr<views::View> CreateBrowserSidebarHost(
    Browser* browser,
    ModalOverlayController* modal_overlay_controller);

// Applies the most recent persistent sidebar mutation when keyboard focus or
// the pointer is inside this Ahoi sidebar. This intentionally declines the
// shortcut elsewhere so page and text-field undo semantics remain untouched.
bool UndoBrowserSidebarMutation(views::View* sidebar_host);

// Global browser accelerators use these narrow entry points so the actual
// workspace/session behavior remains owned by the native sidebar host.
bool ActivateRelativeBrowserWorkspace(views::View* sidebar_host, int delta);
// Cycles through live tabs that belong to the currently active workspace.
// This deliberately does not use TabStripModel's global next/previous helpers,
// because one native tab strip backs every Ahoi workspace.
bool ActivateRelativeBrowserRuntimeTab(views::View* sidebar_host, int delta);
bool ActivateBrowserWorkspaceAtIndex(views::View* sidebar_host, size_t index);
// Activates the folder's workspace, expands its complete ancestor path and
// selects the folder in the native tree.
bool RevealBrowserSidebarFolder(views::View* sidebar_host,
                                const base::Uuid& folder_id);

bool ToggleBrowserSidebarFloating(views::View* sidebar_host);
bool ToggleBrowserSidebarVisibility(views::View* sidebar_host);
bool RestoreBrowserSidebar(views::View* sidebar_host);

// Resolves only Ahoi's private drag identity. A closed saved page is valid but
// remains unopened during hover. A committed drop can activate it through the
// existing host path, which first reuses any already-bound live tab.
BrowserSidebarSplitDropSource ResolveBrowserSidebarSplitDropSource(
    views::View* sidebar_host,
    const drag::SidebarTabDragPayload& payload,
    bool activate_saved_page);

// Clears all sidebar-owned native drag presentation on drop or cancellation.
void CancelBrowserSidebarSplitDropDrag(views::View* sidebar_host);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_H_
