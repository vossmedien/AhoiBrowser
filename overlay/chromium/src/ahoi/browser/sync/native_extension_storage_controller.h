// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_NATIVE_EXTENSION_STORAGE_CONTROLLER_H_
#define AHOI_BROWSER_SYNC_NATIVE_EXTENSION_STORAGE_CONTROLLER_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "ahoi/browser/sync/extension_storage_types.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"

namespace base {
class SequencedTaskRunner;
}

namespace ahoi::sync {

struct PermittedSettingRecord;
class ProfileSyncUiBridge;

// UI-sequence owner of per-key storage applications, not a storage backend or
// consent source. Native owns the actual keyed commit/readback and its original
// authority checks. Local change capture/origin filtering remains with Service
// and Native; no asynchronous global "applying" guard hides unrelated edits.
class NativeExtensionStorageController final {
 public:
  using ChangedCallback =
      base::RepeatingCallback<void(const ExtensionStorageResult&)>;

  NativeExtensionStorageController(base::WeakPtr<ProfileSyncUiBridge> bridge,
                                   ChangedCallback changed);
  ~NativeExtensionStorageController();
  NativeExtensionStorageController(const NativeExtensionStorageController&) =
      delete;
  NativeExtensionStorageController& operator=(
      const NativeExtensionStorageController&) = delete;

  // Accept only an already-merged, complete format3 record from the finite
  // native catalogue. Identical refreshes are deduplicated. A real Native-ready
  // notification or explicit user retry may pass retry=true to replace the old
  // attempt, including a deferred completion still travelling back to this
  // sequence. Ordinary refreshes must never request that explicit replacement.
  void Request(const PermittedSettingRecord& record,
               SyncAuthorization authorization,
               bool retry = false);
  void Cancel(std::string_view setting_id);

  // Latest attempt per full setting ID, not merely extension ID. kDeferred is
  // also the initial not-yet-committed state until Native completes. References
  // must not survive a mutation or a callback that can destroy this controller.
  const std::map<std::string, ExtensionStorageResult>& results() const;

 private:
  struct Operation;

  bool IsPendingOperation(std::string_view setting_id,
                          const base::Uuid& operation_id,
                          std::string_view revision) const
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  void OnNativeResult(std::string setting_id,
                      base::Uuid expected_operation_id,
                      std::string expected_revision,
                      ExtensionStorageResult result);
  void Finish(std::string_view setting_id, ExtensionStorageResult result)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  void Notify(ExtensionStorageResult result)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  const base::WeakPtr<ProfileSyncUiBridge> bridge_;
  const scoped_refptr<base::SequencedTaskRunner> task_runner_;
  const ChangedCallback changed_;
  // Completed attempts are retained so a later refresh cannot accidentally
  // renew an old lease or repeat a failed/deferred native write.
  std::map<std::string, std::unique_ptr<Operation>, std::less<>> operations_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::map<std::string, ExtensionStorageResult> results_
      GUARDED_BY_CONTEXT(sequence_checker_);
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<NativeExtensionStorageController> weak_ptr_factory_{
      this};
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_NATIVE_EXTENSION_STORAGE_CONTROLLER_H_
