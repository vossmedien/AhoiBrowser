// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_DOCUMENT_ACTIONS_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_DOCUMENT_ACTIONS_H_

#include <string_view>

#include "ahoi/browser/developer_toolkit/developer_toolkit_types.h"
#include "content/public/common/isolated_world_ids.h"

namespace ahoi {

// Reserved Ahoi world: the Chromium embedder reserves the first ten custom
// world IDs, while Chrome's built-in worlds occupy the first four. Keeping the
// ID in one public constant prevents UI callers from choosing a main world or
// another product's world by mistake.
inline constexpr int32_t kDeveloperToolkitIsolatedWorldId =
    content::ISOLATED_WORLD_ID_CONTENT_END + 5;

// Returns one of the fixed, audited isolated-world payloads. There is no
// overload that accepts caller-provided JavaScript or CSS.
DocumentActionScript GetDocumentActionScript(DocumentAction action);

// Defensive helper for callers that need to assert the contract before
// passing a payload to WebContents::ExecuteJavaScriptInIsolatedWorld.
bool IsFixedDocumentActionScript(std::string_view source);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_DOCUMENT_ACTIONS_H_
