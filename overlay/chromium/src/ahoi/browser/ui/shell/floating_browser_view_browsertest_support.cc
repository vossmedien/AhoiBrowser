// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/shell/floating_browser_view_browsertest_support.h"

#include <set>

#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "base/memory/weak_ptr.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/drag_controller.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::test_support {

views::View* FindDraggableDescendant(views::View* root) {
  if (!root || !root->GetVisible()) {
    return nullptr;
  }
  if (ahoi::sidebar::GetOpenTabForView(root) && root->drag_controller()) {
    views::DragController* const controller = root->drag_controller();
    const gfx::Point press = root->GetLocalBounds().CenterPoint();
    if (!root->bounds().IsEmpty() &&
        controller->CanStartDragForView(root, press, press)) {
      return root;
    }
  }
  for (views::View* const child : root->children()) {
    if (views::View* const result = FindDraggableDescendant(child)) {
      return result;
    }
  }
  return nullptr;
}

views::View* FindAcceptingDropDescendant(views::View* root,
                                         const ui::OSExchangeData& data) {
  if (!root || !root->GetVisible()) {
    return nullptr;
  }
  for (views::View* const child : root->children()) {
    if (views::View* const result = FindAcceptingDropDescendant(child, data)) {
      return result;
    }
  }
  int formats = 0;
  std::set<ui::ClipboardFormatType> format_types;
  return root->GetDropFormats(&formats, &format_types) &&
                 data.HasAnyFormat(formats, format_types) && root->CanDrop(data)
             ? root
             : nullptr;
}

gfx::Rect BoundsInTarget(views::View* view, views::View* target) {
  return views::View::ConvertRectToTarget(view, target, view->GetLocalBounds());
}

bool IsInsideOrEqual(views::View* ancestor, views::View* candidate) {
  return candidate && (candidate == ancestor || ancestor->Contains(candidate));
}

void CollectProjectedOpenTabs(views::View* root,
                              std::set<tabs::TabInterface*>* open_tabs) {
  if (!root) {
    return;
  }
  if (base::WeakPtr<tabs::TabInterface> tab =
          ahoi::sidebar::GetOpenTabForView(root)) {
    open_tabs->insert(tab.get());
  }
  for (views::View* const child : root->children()) {
    CollectProjectedOpenTabs(child, open_tabs);
  }
}

ahoi::sidebar::SidebarTreeView* FindSidebarTreeView(views::View* root) {
  if (!root) {
    return nullptr;
  }
  if (auto* tree = views::AsViewClass<ahoi::sidebar::SidebarTreeView>(root)) {
    return tree;
  }
  for (views::View* const child : root->children()) {
    if (auto* tree = FindSidebarTreeView(child)) {
      return tree;
    }
  }
  return nullptr;
}

}  // namespace ahoi::test_support
