// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/browser_sidebar_host_state.h"

#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

SidebarDiscoveryState::SidebarDiscoveryState() = default;
SidebarDiscoveryState::~SidebarDiscoveryState() = default;

SidebarThumbnailState::SidebarThumbnailState() = default;
SidebarThumbnailState::~SidebarThumbnailState() = default;

SidebarGroupRecentState::SidebarGroupRecentState() = default;
SidebarGroupRecentState::~SidebarGroupRecentState() = default;

SidebarGroupDialogState::SidebarGroupDialogState() = default;
SidebarGroupDialogState::~SidebarGroupDialogState() = default;

SidebarWorkspaceDialogState::SidebarWorkspaceDialogState() = default;
SidebarWorkspaceDialogState::~SidebarWorkspaceDialogState() = default;

CrossLevelMoveRequest::CrossLevelMoveRequest() = default;
CrossLevelMoveRequest::CrossLevelMoveRequest(const CrossLevelMoveRequest&) =
    default;
CrossLevelMoveRequest& CrossLevelMoveRequest::operator=(
    const CrossLevelMoveRequest&) = default;
CrossLevelMoveRequest::~CrossLevelMoveRequest() = default;

SidebarContextMenuState::SidebarContextMenuState() = default;
SidebarContextMenuState::~SidebarContextMenuState() = default;

}  // namespace ahoi::sidebar
