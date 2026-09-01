// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "ahoi/browser/ui/modal_overlay_controller.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/move_destination_menu_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_recent_links_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/i18n/case_conversion.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/pickle.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/base_window.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/drag_controller.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

std::unique_ptr<views::View> CreateBrowserSidebarHost(
    Browser* browser,
    ModalOverlayController* modal_overlay_controller) {
  if (!browser || !browser->is_type_normal() || !browser->GetProfile() ||
      !browser->GetProfile()->IsRegularProfile() ||
      browser->GetProfile()->IsOffTheRecord() || !modal_overlay_controller) {
    return nullptr;
  }
  SessionBridge* session_bridge =
      SessionBridgeFactory::GetForProfile(browser->GetProfile());
  WorkspaceService* workspace_service =
      WorkspaceServiceFactory::GetForProfile(browser->GetProfile());
  if (!session_bridge || !session_bridge->is_operational() ||
      !session_bridge->tab_tree_store() || !workspace_service) {
    return nullptr;
  }
  return std::make_unique<BrowserSidebarHostView>(
      browser, session_bridge, workspace_service, modal_overlay_controller);
}

void NotifyBrowserSidebarPresentationSettled(views::View* sidebar_host) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  if (host) {
    host->OnSidebarPresentationSettled();
  }
}

bool UndoBrowserSidebarMutation(views::View* sidebar_host) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host && host->UndoLastMutationIfAvailable();
}

bool ActivateRelativeBrowserWorkspace(views::View* sidebar_host, int delta) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host && host->ActivateRelativeWorkspace(delta);
}

bool ActivateRelativeBrowserWorkspaceByGesture(views::View* sidebar_host,
                                               int delta) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host && host->ActivateRelativeWorkspaceByGesture(delta);
}

base::WeakPtr<tabs::TabInterface> ResolveRelativeBrowserRuntimeTab(
    views::View* sidebar_host,
    int delta) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host ? host->ResolveRelativeRuntimeTab(delta) : nullptr;
}

bool ActivateRelativeBrowserRuntimeTab(views::View* sidebar_host, int delta) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host && host->ActivateRelativeRuntimeTab(delta);
}

bool ActivateBrowserWorkspaceAtIndex(views::View* sidebar_host, size_t index) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host && host->ActivateWorkspaceAtIndex(index);
}

bool RevealBrowserSidebarFolder(views::View* sidebar_host,
                                const base::Uuid& folder_id) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host && host->RevealFolder(folder_id);
}

bool ToggleBrowserSidebarFloating(views::View* sidebar_host) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host && host->ToggleFloatingSidebar();
}

bool ToggleBrowserSidebarVisibility(views::View* sidebar_host) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host && host->ToggleSidebarVisibility();
}

bool RestoreBrowserSidebar(views::View* sidebar_host) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host && host->RestoreSidebar();
}

bool ToggleBrowserSidebarDiscovery(views::View* sidebar_host) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  if (!host || !host->discovery_view_) {
    return false;
  }
  host->ToggleSidebarDiscovery();
  return true;
}

BrowserSidebarSplitDropSource ResolveBrowserSidebarSplitDropSource(
    views::View* sidebar_host,
    const drag::SidebarTabDragPayload& payload,
    bool activate_saved_page) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host ? host->ResolveSplitDropSource(payload, activate_saved_page)
              : BrowserSidebarSplitDropSource();
}

void BeginBrowserSidebarSplitPaneDrag(
    views::View* sidebar_host,
    const drag::SidebarTabDragPayload& payload) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  if (host) {
    host->BeginSplitPaneDrag(payload);
  }
}

void CancelBrowserSidebarSplitDropDrag(views::View* sidebar_host) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  if (host) {
    host->CancelSplitDropDrag();
  }
}

void ClearBrowserSidebarDropTargetPresentation(views::View* sidebar_host) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  if (host) {
    host->ClearDropTargetPresentation();
  }
}

bool IsBrowserSidebarDragActive(views::View* sidebar_host) {
  auto* host = views::AsViewClass<BrowserSidebarHostView>(sidebar_host);
  return host && host->IsSidebarDragActive();
}

}  // namespace ahoi::sidebar
