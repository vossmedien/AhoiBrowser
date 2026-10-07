// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_TAB_GROUP_SIDEBAR_ADAPTER_VIEWS_H_
#define AHOI_BROWSER_EXTENSIONS_TAB_GROUP_SIDEBAR_ADAPTER_VIEWS_H_

#include <memory>

#include "ahoi/browser/extensions/tab_group_sidebar_adapter.h"
#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "base/functional/callback.h"

namespace views {
class View;
}

namespace ahoi::extensions {

using ResolveGroupDropTab = base::RepeatingCallback<
    base::WeakPtr<tabs::TabInterface>(const drag::SidebarTabDragPayload&)>;

// The host inserts its existing normal/split row Views into `contents`.
// This view is only a native folder presentation and drop/editor surface.
std::unique_ptr<views::View> CreateTabGroupSidebarFolder(
    base::WeakPtr<TabGroupSidebarAdapter> adapter,
    const SidebarTabGroup& group,
    bool reveal_for_search,
    ResolveGroupDropTab resolve_tab,
    base::RepeatingClosure clear_drop_targets,
    base::RepeatingClosure finish_drag,
    views::View** contents);

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_TAB_GROUP_SIDEBAR_ADAPTER_VIEWS_H_
