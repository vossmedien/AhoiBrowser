// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_LINK_COPY_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_LINK_COPY_H_

#include <optional>
#include <string>
#include <string_view>

#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/uuid.h"
#include "url/gurl.h"

namespace ahoi::sidebar {

enum class PageLinkCopyFormat {
  kUrl = 0,
  kMarkdown,
};

// Produces a single credential-free HTTP(S) link for the system clipboard.
// Markdown labels and destinations are escaped so the copied value remains one
// well-formed inline link even when a page title contains markup characters.
[[nodiscard]] std::optional<std::u16string> BuildPageLinkClipboardText(
    const GURL& url,
    std::u16string_view title,
    PageLinkCopyFormat format);

// Builds a newline-delimited URL list in the same depth-first manual order as
// the native sidebar. When `folder_id` is empty the complete workspace is
// traversed; otherwise only descendants of that folder are included.
[[nodiscard]] tab_tree::TabTreeStore::Result BuildOrderedLinkList(
    tab_tree::TabTreeStore* store,
    const base::Uuid& workspace_id,
    std::optional<base::Uuid> folder_id,
    std::u16string* links);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_LINK_COPY_H_
