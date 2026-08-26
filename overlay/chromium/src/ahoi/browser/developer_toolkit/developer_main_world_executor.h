// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_MAIN_WORLD_EXECUTOR_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_MAIN_WORLD_EXECUTOR_H_

#include <string_view>

namespace content {
class WebContents;
}

namespace ahoi {

// Executes one explicitly authorized script in the active page's Main World
// through a transient DevTools protocol session. The session detaches after
// exactly one Runtime.evaluate response and owns no idle observer or renderer.
// Returns whether dispatch was accepted, not whether page code completed.
bool ExecuteDeveloperJavaScriptInMainWorld(content::WebContents* web_contents,
                                           std::string_view source);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_MAIN_WORLD_EXECUTOR_H_
