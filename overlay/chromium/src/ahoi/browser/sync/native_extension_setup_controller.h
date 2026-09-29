// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_NATIVE_EXTENSION_SETUP_CONTROLLER_H_
#define AHOI_BROWSER_SYNC_NATIVE_EXTENSION_SETUP_CONTROLLER_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "ahoi/browser/sync/extension_setup_types.h"
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

// Owns per-extension native restore attempts on the profile's UI sequence.
// It neither captures local user intent nor acquires category/account consent.
// Native retains install, policy and permission authority; no result here is a
// provider acknowledgement or proof that another device applied the record.
class NativeExtensionSetupController final {
 public:
  using ChangedCallback =
      base::RepeatingCallback<void(const ExtensionRestoreResult&)>;

  NativeExtensionSetupController(base::WeakPtr<ProfileSyncUiBridge> bridge,
                                 ChangedCallback changed);
  ~NativeExtensionSetupController();
  NativeExtensionSetupController(const NativeExtensionSetupController&) =
      delete;
  NativeExtensionSetupController& operator=(
      const NativeExtensionSetupController&) = delete;

  // Only a complete, validated current-format desired-state record is accepted.
  // Identical refreshes never repeat prompts/downloads, including after failure
  // or cancellation. An explicit user retry can replace the same revision;
  // stale/conflicting records cannot replace a newer accepted configuration.
  void Request(const PermittedSettingRecord& record,
               SyncAuthorization authorization,
               bool user_initiated = false);
  void Cancel(std::string_view extension_id);
  void CancelAll();

  // Latest attempt per extension. References must not outlive a mutation or a
  // changed callback, which may synchronously destroy this controller.
  const std::map<std::string, ExtensionRestoreResult>& results() const;

 private:
  struct Operation;

  bool IsCurrentOperation(std::string_view extension_id,
                          const base::Uuid& operation_id,
                          std::string_view revision) const
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  void OnNativeResult(std::string extension_id,
                      base::Uuid expected_operation_id,
                      std::string expected_revision,
                      ExtensionRestoreResult result);
  void RefreshCompletedOperation(ExtensionRestoreRequest request)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  void Finish(std::string_view extension_id, ExtensionRestoreResult result)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  // Copies both the callback and result before invoking foreign code. Callers
  // that continue afterwards must recheck lifetime and operation identity.
  void Notify(ExtensionRestoreResult result)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  const base::WeakPtr<ProfileSyncUiBridge> bridge_;
  const scoped_refptr<base::SequencedTaskRunner> task_runner_;
  const ChangedCallback changed_;
  // Completed attempts remain remembered to prevent automatic retry loops.
  std::map<std::string, std::unique_ptr<Operation>, std::less<>> operations_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::map<std::string, ExtensionRestoreResult> results_
      GUARDED_BY_CONTEXT(sequence_checker_);
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<NativeExtensionSetupController> weak_ptr_factory_{this};
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_NATIVE_EXTENSION_SETUP_CONTROLLER_H_
