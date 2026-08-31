// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_support.h"

#include <optional>
#include <string>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/image_model.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {

bool CanDetachRuntimeSplitPaneOnSelfDrop(bool source_is_split,
                                         OpenTabDropPosition position) {
  return source_is_split && position != OpenTabDropPosition::kSplit;
}

void WriteOpenTabDragPayload(ui::OSExchangeData* data,
                             std::optional<base::Uuid> saved_node_id,
                             int runtime_tab_handle,
                             const std::u16string& fallback_title) {
  if (saved_node_id.has_value()) {
    drag::WriteSavedSidebarTabDragPayload(data, *saved_node_id, fallback_title);
    return;
  }
  drag::WriteRuntimeSidebarTabDragPayload(data, runtime_tab_handle,
                                          fallback_title);
}

ui::ImageModel GetLiveTabFavicon(tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  favicon::ContentFaviconDriver* driver =
      contents ? favicon::ContentFaviconDriver::FromWebContents(contents)
               : nullptr;
  return driver ? ui::ImageModel::FromImage(driver->GetFavicon())
                : ui::ImageModel();
}

namespace internal {

bool IsNewTabPage(tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents) {
    return false;
  }
  GURL url = contents->GetVisibleURL();
  if (!url.is_valid() || url.is_empty()) {
    url = contents->GetLastCommittedURL();
  }
  return url == GURL(chrome::kChromeUINewTabURL);
}

std::u16string StableTabTitle(tabs::TabInterface* tab) {
  return !tab || tab->GetTitle().empty()
             ? l10n_util::GetStringUTF16(IDS_NEW_TAB)
             : tab->GetTitle();
}

}  // namespace internal

void ClearOpenTabRowDropTargetPresentation(views::View* root,
                                           views::View* except) {
  if (!root) {
    return;
  }
  if (root != except) {
    internal::ClearOpenTabRowDropTargetPresentationForView(root);
  }
  // Known row cleanup changes paint/layout state only; it never mutates this
  // hierarchy, so traversing composite split containers remains stable.
  for (views::View* child : root->children()) {
    ClearOpenTabRowDropTargetPresentation(child, except);
  }
}

}  // namespace ahoi::sidebar
