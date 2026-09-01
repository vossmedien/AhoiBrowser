// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_H_
#define AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_H_

#include <cstddef>
#include <memory>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
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
  base::WeakPtr<tabs::TabInterface> tab;
  // Present only when commit-time materialization changed browser state. A
  // failed drop runs this exactly once to restore the previous active tab and
  // close only the tab actually opened by this transaction. Destroying the
  // callback commits the materialization and performs no work.
  base::OnceClosure rollback;
};

// Creates the profile-backed Ahoi organization surface for a normal browser
// window. Unsupported and private profiles deliberately return nullptr.
std::unique_ptr<views::View> CreateBrowserSidebarHost(
    Browser* browser,
    ModalOverlayController* modal_overlay_controller);

// Notifies a production Ahoi host that its presentation animation and final
// layout pass have settled. Dummy/non-Ahoi hosts are deliberately ignored.
void NotifyBrowserSidebarPresentationSettled(views::View* sidebar_host);

// Applies the most recent persistent sidebar mutation when keyboard focus or
// the pointer is inside this Ahoi sidebar. This intentionally declines the
// shortcut elsewhere so page and text-field undo semantics remain untouched.
bool UndoBrowserSidebarMutation(views::View* sidebar_host);

// Global browser accelerators use these narrow entry points so the actual
// workspace/session behavior remains owned by the native sidebar host.
bool ActivateRelativeBrowserWorkspace(views::View* sidebar_host, int delta);
// Gesture entry point stays separate so WorkspaceService observers receive the
// truthful source while keyboard commands keep their existing semantics.
bool ActivateRelativeBrowserWorkspaceByGesture(views::View* sidebar_host,
                                               int delta);
// Cycles through live tabs that belong to the currently active workspace.
// This deliberately does not use TabStripModel's global next/previous helpers,
// because one native tab strip backs every Ahoi workspace.
base::WeakPtr<tabs::TabInterface> ResolveRelativeBrowserRuntimeTab(
    views::View* sidebar_host,
    int delta);
bool ActivateRelativeBrowserRuntimeTab(views::View* sidebar_host, int delta);
bool ActivateBrowserWorkspaceAtIndex(views::View* sidebar_host, size_t index);
// Activates the folder's workspace, expands its complete ancestor path and
// selects the folder in the native tree.
bool RevealBrowserSidebarFolder(views::View* sidebar_host,
                                const base::Uuid& folder_id);

bool ToggleBrowserSidebarFloating(views::View* sidebar_host);
bool ToggleBrowserSidebarVisibility(views::View* sidebar_host);
bool RestoreBrowserSidebar(views::View* sidebar_host);
bool ToggleBrowserSidebarDiscovery(views::View* sidebar_host);

// Resolves only Ahoi's private drag identity. A closed saved page is valid but
// remains unopened during hover. A committed drop can activate it through the
// existing host path, which first reuses any already-bound live tab.
BrowserSidebarSplitDropSource ResolveBrowserSidebarSplitDropSource(
    views::View* sidebar_host,
    const drag::SidebarTabDragPayload& payload,
    bool activate_saved_page);

// Publishes the stable identity of a split-pane drag that originated in the
// WebContents mini toolbar. This activates the same visible sidebar targets as
// a drag that starts on a sidebar row; Widget drag completion remains the
// authoritative cancellation boundary.
void BeginBrowserSidebarSplitPaneDrag(
    views::View* sidebar_host,
    const drag::SidebarTabDragPayload& payload);

// Clears all sidebar-owned native drag presentation on drop or cancellation.
void CancelBrowserSidebarSplitDropDrag(views::View* sidebar_host);

// Clears only concrete target highlights while retaining the current drag
// source and drag-only targets. WebContents split-drop routing uses this when
// the pointer leaves the sidebar so stale AppKit exit delivery cannot leave a
// second accepted surface painted behind the content overlay.
void ClearBrowserSidebarDropTargetPresentation(views::View* sidebar_host);

// Returns whether this host currently owns a saved- or runtime-tab drag. The
// BrowserView hit-test seam uses this narrow query to route native drag events
// through floating chrome without changing normal toolbar input behavior.
bool IsBrowserSidebarDragActive(views::View* sidebar_host);

// Native drags may cross Ahoi windows. The source host updates this
// process-local lease, while every floating target window consults it before
// allowing its higher-painted navigation surface to participate in hit tests.
void SetBrowserSidebarDragRoutingActive(views::View* sidebar_host, bool active);
bool IsAnyBrowserSidebarDragActive();

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_H_
