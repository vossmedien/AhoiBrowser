// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_PREPROCESSOR_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_PREPROCESSOR_H_

#include "ahoi/browser/developer_toolkit/developer_style_compiler.h"

namespace ahoi {

// Deterministic, resource-bounded LESS/SASS compiler core. It accepts no
// paths, import callbacks, URLs or external functions and is intended to run
// exclusively behind the kService-sandboxed Mojo service.
DeveloperStyleCompileResult CompileDeveloperStyleSource(
    DeveloperStyleCompileRequest request);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_STYLE_PREPROCESSOR_H_
