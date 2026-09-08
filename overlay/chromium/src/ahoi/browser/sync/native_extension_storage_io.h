// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_NATIVE_EXTENSION_STORAGE_IO_H_
#define AHOI_BROWSER_SYNC_NATIVE_EXTENSION_STORAGE_IO_H_

#include <string>
#include <vector>

#include "ahoi/browser/sync/extension_storage_types.h"
#include "base/functional/callback.h"
#include "base/values.h"

namespace value_store {
class ValueStore;
}

namespace ahoi::sync {

struct NativeExtensionStorageCommit {
  ExtensionStorageResult result;
  // Exact safe changes from a successful native WriteResult. They can be
  // present even if subsequent readback/revocation prevents kStored. Native UI
  // must dispatch them with remote-apply origin before completing the request.
  base::Value changes = base::Value(base::DictValue());
};

// Called only by StorageFrontend::RunWithStorage on its storage sequence.
// Profile/extension eligibility and sync-area selection remain the caller's
// responsibility; a full capture must supply its full reviewed key set.
// Both functions post their response to the UI task runner;
// they never invoke observers, extension JavaScript, or Google's Sync APIs.
void ReadNativeExtensionStorage(
    std::string extension_id,
    std::vector<std::string> keys,
    SyncAuthorization authorization,
    base::OnceCallback<void(NativeExtensionStorageSnapshot)> callback,
    value_store::ValueStore* store);

void WriteNativeExtensionStorage(
    ExtensionStorageRequest request,
    base::OnceCallback<void(NativeExtensionStorageCommit)> callback,
    value_store::ValueStore* store);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_NATIVE_EXTENSION_STORAGE_IO_H_
