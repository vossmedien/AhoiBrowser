// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_NATIVE_EXTENSION_SETUP_OPERATION_H_
#define AHOI_BROWSER_EXTENSIONS_NATIVE_EXTENSION_SETUP_OPERATION_H_

#include <memory>

#include "ahoi/browser/sync/extension_setup_types.h"
#include "base/functional/callback.h"
#include "ui/gfx/native_ui_types.h"

class Profile;

namespace ahoi::extensions {

// Session-owned native work. Destruction permanently revokes its original
// lease, including installers continuing on the shared-file sequence.
class NativeExtensionSetupOperation {
 public:
  virtual ~NativeExtensionSetupOperation() = default;
  virtual void Start() = 0;
};

std::unique_ptr<NativeExtensionSetupOperation>
CreateNativeExtensionSetupOperation(
    Profile* profile,
    gfx::NativeWindow parent_window,
    sync::ExtensionRestoreRequest request,
    base::OnceCallback<void(sync::ExtensionRestoreResult)> completion);

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_NATIVE_EXTENSION_SETUP_OPERATION_H_
