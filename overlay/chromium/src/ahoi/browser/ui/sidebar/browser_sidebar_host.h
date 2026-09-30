// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_H_
#define AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_H_

#include <cstddef>
#include <memory>
#include <optional>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/uuid.h"

class Browser;

namespace gfx {
class Point;
}

namespace tabs {
class TabInterface;
}

namespace views {
class MenuRunner;
class View;
}

namespace ahoi {
class ModalOverlayController;
}

namespace ahoi::sidebar {

enum class PageLinkCopyFormat;

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
// Horizontal bookmark scrolling owns gestures that begin over its shelf.
bool CanStartBrowserWorkspaceGesture(views::View* sidebar_host,
                                     const gfx::Point& screen_point);
// Cycles through live tabs that belong to the currently active workspace.
// This deliberately does not use TabStripModel's global next/previous helpers,
// because one native tab strip backs every Ahoi workspace.
base::WeakPtr<tabs::TabInterface> ResolveRelativeBrowserRuntimeTab(
    views::View* sidebar_host,
    int delta);
bool ActivateRelativeBrowserRuntimeTab(views::View* sidebar_host, int delta);
// Keyboard tab stepping (next/previous tab, Cmd+1..9) follows the sidebar's
// tab stops of the active Workspace instead of the window-wide tab strip:
// saved rows, then temporary rows, a split as one stop, collapsed folders
// skipped, wrapping at both ends. Returns false when `sidebar_host` is not an
// Ahoi host so Chromium keeps its strip order there. Otherwise the command
// belongs to Ahoi and `target` is the tab to activate, or null when the step
// has nothing to activate; it never falls back to another Workspace's tab.
bool ResolveBrowserSidebarTabStep(views::View* sidebar_host,
                                  int delta,
                                  base::WeakPtr<tabs::TabInterface>* target);
// `index` counts the tab stops from zero; std::nullopt is the last stop.
bool ResolveBrowserSidebarNumberedTab(
    views::View* sidebar_host,
    std::optional<size_t> index,
    base::WeakPtr<tabs::TabInterface>* target);
bool ActivateBrowserWorkspaceAtIndex(views::View* sidebar_host, size_t index);
// Any Workspace of the shared switcher, including another Profile's (ADR 0011
// step 2): switches in place or hands the window's frame over.
bool ActivateBrowserWorkspaceById(views::View* sidebar_host,
                                  const base::Uuid& workspace_id);
// Activates the folder's workspace, expands its complete ancestor path and
// selects the folder in the native tree.
bool RevealBrowserSidebarFolder(views::View* sidebar_host,
                                const base::Uuid& folder_id);
// ADR 0012 section 2 (command bar "In Workspace verschieben"): moves the
// selected folder, else the active page with its split group or the active
// temporary tab, to the root of another Workspace of this Profile.
bool CanMoveBrowserSidebarSelectionToWorkspace(views::View* sidebar_host,
                                               const base::Uuid& workspace_id);
bool MoveBrowserSidebarSelectionToWorkspace(views::View* sidebar_host,
                                            const base::Uuid& workspace_id);
// ADR 0012 section 1 (command bar "Zusammenführen mit …"): opens the
// Workspace menu's merge confirmation for the window's shown Workspace into
// `target_id`, a Workspace of the same Profile, once the command bar has
// closed. The merge runs only when the user confirms that dialog.
bool CanShowBrowserSidebarWorkspaceMerge(views::View* sidebar_host,
                                         const base::Uuid& target_id);
bool ShowBrowserSidebarWorkspaceMerge(views::View* sidebar_host,
                                      const base::Uuid& target_id);

bool ToggleBrowserSidebarFloating(views::View* sidebar_host);
bool ToggleBrowserSidebarVisibility(views::View* sidebar_host);
bool RestoreBrowserSidebar(views::View* sidebar_host);
bool ToggleBrowserSidebarDiscovery(views::View* sidebar_host);

// Compact page actions always resolve the currently active split pane at
// execution time. They never materialize a saved page or persist clipboard
// contents into Ahoi profile/sync state, including in off-the-record windows.
bool CanCopyActivePageLink(Browser* browser);
bool CopyActivePageLink(Browser* browser, PageLinkCopyFormat format);
bool CanOpenActivePageInReadingMode(Browser* browser);
bool OpenActivePageInReadingMode(Browser* browser);

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

// Returns whether a context menu opened from this host is still showing. An
// edge-revealed hidden sidebar stays open underneath it: the menu may extend
// past the sidebar, so the pointer over the menu is not a reason to retract.
// Callers that poll this re-check after the menu has closed.
bool IsBrowserSidebarMenuRunning(views::View* sidebar_host);

// Test seam: installs `runner` as the host's context menu runner, as the
// context menu paths do while their menu is showing. nullptr releases it.
void SetBrowserSidebarContextMenuRunnerForTesting(
    views::View* sidebar_host,
    std::unique_ptr<views::MenuRunner> runner);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_BROWSER_SIDEBAR_HOST_H_
