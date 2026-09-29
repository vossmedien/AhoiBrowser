// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_ui_bridge.h"

#include <utility>

namespace ahoi::sync {

NativeExtensionSetupSnapshot ProfileSyncUiBridge::ReadNativeExtensionSetup() {
  return {};
}

void ProfileSyncUiBridge::ApplyNativeExtensionSetup(
    ExtensionRestoreRequest request,
    base::OnceCallback<void(ExtensionRestoreResult)> completion) {
  std::move(completion)
      .Run({.operation_id = request.operation_id,
            .revision = std::move(request.revision),
            .disposition = ExtensionRestoreDisposition::kUnsupported});
}

SharedTabNativeSupport ProfileSyncUiBridge::GetSharedTabNativeSupport() const {
  return {};
}

bool ProfileSyncUiBridge::ExportTabTreeSyncSnapshot(
    tab_tree::TabTreeSnapshot* snapshot,
    std::string* baseline_receipt) {
  return false;
}

void ProfileSyncUiBridge::ApplySyncedTabTreeSnapshotWithReceipt(
    tab_tree::TabTreeSnapshot snapshot,
    std::string baseline_receipt,
    base::RepeatingCallback<bool()> authorization,
    base::OnceCallback<void(tab_tree::TabTreeStore::Result)> completion) {
  std::move(completion).Run(tab_tree::TabTreeStore::Result::kInvalidArgument);
}

}  // namespace ahoi::sync
