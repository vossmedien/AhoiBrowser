// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_PROFILE_SYNC_UI_BRIDGE_H_
#define AHOI_BROWSER_SYNC_PROFILE_SYNC_UI_BRIDGE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ahoi/browser/sync/extension_setup_types.h"
#include "ahoi/browser/sync/shared_tab_sync_types.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/uuid.h"
#include "url/gurl.h"

namespace ahoi::sync {

// UI-free boundary implemented by the profile's regular SessionBridge. The
// sync service owns no Browser, window, tab, or SessionBridge factory and can
// therefore remain below chrome/browser/ui in the dependency graph.
class ProfileSyncUiBridge {
 public:
  virtual ~ProfileSyncUiBridge() = default;

  // SessionBridge invalidates this before releasing its browser/tab objects,
  // so profile shutdown ordering can never leave the sync service with a
  // dangling UI pointer.
  virtual base::WeakPtr<ProfileSyncUiBridge> GetWeakPtrForSync() = 0;

  virtual base::CallbackListSubscription AddTabTreeSnapshotChangedCallback(
      base::RepeatingCallback<void(const tab_tree::TabTreeSnapshot&)>
          callback) = 0;
  // Requests a fresh snapshot from every attached runtime window. The bridge
  // fans this out to its UI hosts; each host reads its live TabStripModel and
  // publishes through ProfileSyncService::PublishWindowTabs().
  virtual void RequestLocalTabCapture() = 0;
  // Explicit implementation support, separate from wire-format membership.
  // Native capture responds with the same Service-issued generation; absent
  // support must preserve state and never fall back to an empty tab vector.
  virtual SharedTabNativeSupport GetSharedTabNativeSupport() const;
  virtual void RequestSharedTabCapture(uint64_t generation) {}
  // Complete regular-profile inventory only. A missing/partial result is not
  // permission to uninstall. Remote desired state is applied through native
  // verified install/enable/uninstall machinery, never by copying binaries or
  // preferences. kApplied requires an actual matching native readback; pending
  // downloads/prompts must be reported honestly. The original authorization
  // stays revoked across asynchronous hops and a later reapproval.
  virtual NativeExtensionSetupSnapshot ReadNativeExtensionSetup();
  virtual void ApplyNativeExtensionSetup(
      ExtensionRestoreRequest request,
      base::OnceCallback<void(ExtensionRestoreResult)> completion);
  [[nodiscard]] virtual bool ExportTabTreeSnapshot(
      tab_tree::TabTreeSnapshot* snapshot) = 0;
  [[nodiscard]] virtual tab_tree::TabTreeStore::Result
  ApplySyncedTabTreeSnapshot(tab_tree::TabTreeSnapshot snapshot) = 0;

  // Local crash-safety metadata, never a sync field. The native Store must
  // commit this opaque receipt in the SAME transaction as the projected tree,
  // preserve it across ordinary local edits and return both atomically. Check
  // the original authorization before apply and immediately before committing;
  // carry it through an asynchronous persistence hop without renewing it.
  // Without this seam a post-apply crash could turn remote values into new
  // local edits.
  // Export true attests the COMPLETE CURRENT tree+receipt are also durably
  // committed. Return false during load/pending local persistence; do not
  // return an older disk snapshot while newer RAM edits exist. Notify the
  // existing local snapshot callback again when that same current revision is
  // durable. Otherwise a crash after Common observes RAM=B but before Native
  // stores B could turn the restored disk=A into a spurious local undo on the
  // next run.
  [[nodiscard]] virtual bool ExportTabTreeSyncSnapshot(
      tab_tree::TabTreeSnapshot* snapshot,
      std::string* baseline_receipt);
  // Complete only after durable native success/readback. While disk work is
  // pending, ordinary local snapshot callbacks MUST stay live and revoke this
  // apply. Suppress only the sync-origin callback during final RAM publication;
  // UI observers still receive the native tree change.
  virtual void ApplySyncedTabTreeSnapshotWithReceipt(
      tab_tree::TabTreeSnapshot snapshot,
      std::string baseline_receipt,
      base::RepeatingCallback<bool()> authorization,
      base::OnceCallback<void(tab_tree::TabTreeStore::Result)> completion);

  [[nodiscard]] virtual bool OpenNormalTabFromRemoteCommand(
      const GURL& url,
      std::optional<base::Uuid> workspace_id) = 0;
  [[nodiscard]] virtual bool FocusNormalTabFromRemoteCommand(
      std::string_view local_stable_key) = 0;
  [[nodiscard]] virtual bool CloseNormalTabFromRemoteCommand(
      std::string_view local_stable_key) = 0;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_PROFILE_SYNC_UI_BRIDGE_H_
