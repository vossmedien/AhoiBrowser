// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_BOOKMARK_SYNC_JOURNAL_H_
#define AHOI_BROWSER_SYNC_BOOKMARK_SYNC_JOURNAL_H_

#include <map>
#include <optional>
#include <string>

#include "ahoi/browser/sync/bookmark_sync_bridge_types.h"
#include "base/memory/raw_ptr.h"

namespace ahoi::sync {

class HybridLogicalClock;
class SyncStore;

// The native identity/baseline ledger lives in the existing SyncStore SQLite
// transaction, not another domain database. Only immutable value snapshots may
// enter this backend-sequence class. Native GUIDs remain BookmarkModel-owned.
class BookmarkSyncJournal {
 public:
  explicit BookmarkSyncJournal(SyncStore* store);
  ~BookmarkSyncJournal();

  std::optional<BookmarkSyncProjection> ReconcileLocal(
      const NativeBookmarkSnapshot& snapshot,
      HybridLogicalClock* clock);
  bool AcknowledgeNativeProjection(const NativeBookmarkSnapshot& snapshot);
  std::optional<BookmarkSyncProjection> ReadProjection();

 private:
  struct Binding {
    base::Uuid id;
    std::optional<BookmarkRecord> baseline;
    size_t index = 0;
    bool materialized = false;
    std::optional<BookmarkRecord> last_observed;
    std::string observed_receipt;
    std::string observation_session;
  };
  using Bindings = std::map<std::string, Binding>;

  bool LoadBindings(Bindings* bindings) const;
  bool WriteBinding(const std::string& key, const Binding& binding);
  bool ResolveBindings(const NativeBookmarkSnapshot& snapshot,
                       Bindings* bindings);
  bool LoadEffectiveBaselines(const NativeBookmarkSnapshot& snapshot,
                              Bindings* bindings) const;
  std::optional<BookmarkRecord> ReceiptBaseline(const std::string& receipt,
                                                const base::Uuid& id) const;
  std::optional<std::string> PlanReceipt(const BookmarkRecord& record);
  bool ProjectNative(const NativeBookmarkSnapshot& snapshot,
                     const Bindings& bindings,
                     std::map<std::string, BookmarkRecord>* records) const;
  bool WriteChangedRecord(BookmarkRecord record, HybridLogicalClock* clock);

  const raw_ptr<SyncStore> store_;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_BOOKMARK_SYNC_JOURNAL_H_
