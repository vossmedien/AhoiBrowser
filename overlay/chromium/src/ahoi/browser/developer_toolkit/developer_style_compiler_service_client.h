// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_COMPILER_SERVICE_CLIENT_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_COMPILER_SERVICE_CLIENT_H_

#include <memory>

#include "ahoi/browser/developer_toolkit/developer_style_compiler.h"

namespace ahoi {

// Creates the browser-side adapter. Construction itself does not launch a
// process; its first Compile() launches the kService-sandboxed Utility remote.
std::unique_ptr<DeveloperStyleCompilerService>
CreateSandboxedDeveloperStyleCompilerService();

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_COMPILER_SERVICE_CLIENT_H_
