// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_NATIVE_TREE_SYNC_JOURNAL_H_
#define AHOI_BROWSER_SYNC_NATIVE_TREE_SYNC_JOURNAL_H_

#include <optional>
#include <string>

#include "ahoi/browser/sync/profile_shared_tab_types.h"
#include "base/memory/raw_ptr.h"

namespace ahoi::sync {

class HybridLogicalClock;
class SyncStore;

// Local-only value baselines in the existing SQLite store. Opaque receipts
// are installed atomically by the Native Store, so restart can distinguish
// deliberate local field changes from an already-applied remote projection.
// No fabricated field clocks, second sync format or transport is introduced.
class NativeTreeSyncJournal {
 public:
  explicit NativeTreeSyncJournal(SyncStore* store);
  ~NativeTreeSyncJournal();

  bool ReconcileLocal(const NativeTreeSyncSnapshot& native,
                      HybridLogicalClock* clock,
                      const SyncAuthorization& read_authorization,
                      const SyncAuthorization& write_authorization,
                      bool* wrote_records);
  std::optional<std::string> PlanProjection(
      const tab_tree::TabTreeSnapshot& tree,
      const SyncAuthorization& authorization);

 private:
  const raw_ptr<SyncStore> store_;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_NATIVE_TREE_SYNC_JOURNAL_H_
