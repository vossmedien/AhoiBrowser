// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_PROFILE_SYNC_UI_BRIDGE_H_
#define AHOI_BROWSER_SYNC_PROFILE_SYNC_UI_BRIDGE_H_

#include <cstdint>
#include <optional>
#include <string_view>

#include "ahoi/browser/sync/shared_tab_sync_types.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
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
  virtual SharedTabNativeSupport GetSharedTabNativeSupport() const { return {}; }
  virtual void RequestSharedTabCapture(uint64_t generation) {}
  [[nodiscard]] virtual bool ExportTabTreeSnapshot(
      tab_tree::TabTreeSnapshot* snapshot) = 0;
  [[nodiscard]] virtual tab_tree::TabTreeStore::Result
  ApplySyncedTabTreeSnapshot(tab_tree::TabTreeSnapshot snapshot) = 0;

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
