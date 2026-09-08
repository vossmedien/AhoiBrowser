// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/native_extension_setup_inventory.h"
#include "ahoi/browser/session/session_bridge.h"

namespace ahoi {

sync::NativeExtensionSetupSnapshot SessionBridge::ReadNativeExtensionSetup() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !profile_) {
    return {};
  }
  return extensions::ReadNativeExtensionSetupInventory(profile_);
}

}  // namespace ahoi
