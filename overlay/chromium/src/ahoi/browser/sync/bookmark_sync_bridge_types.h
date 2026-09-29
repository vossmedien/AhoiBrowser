// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_BOOKMARK_SYNC_BRIDGE_TYPES_H_
#define AHOI_BROWSER_SYNC_BOOKMARK_SYNC_BRIDGE_TYPES_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/sync/sync_model.h"
#include "base/functional/callback.h"

namespace ahoi::sync {

inline constexpr char kBookmarkApplyReceiptMetaKey[] =
    "ahoi.sync.apply_receipt";

// Local-only, thread-safe check of the original consent/account scope. It
// carries no profile/node pointer, secret or wire field. Empty is unauthorized
// at the production backend/service boundary.
using BookmarkSyncAuthorization = base::RepeatingCallback<bool()>;

// Value-only snapshots cross the UI/backend sequence boundary. No native node
// pointer or browser runtime ID is a wire identity. Storage prefixes avoid the
// native Local/Account GUID collision domain; keys never leave this profile.
struct NativeBookmarkEntry {
  std::string native_key;
  // Captured from the same native object before a model move. This is a trusted
  // process-local observation, not a caller-supplied bookmark MetaInfo claim.
  std::optional<std::string> previous_native_key;
  std::optional<std::string> parent_key;
  std::optional<BookmarkRoot> root;
  BookmarkKind kind = BookmarkKind::kFolder;
  size_t index = 0;
  std::string title;
  std::string url;
  base::Time created_at;
  // Locally verifiable apply intent, persisted atomically with native fields.
  // An imported/copied MetaInfo token never grants logical identity ownership.
  std::string apply_receipt;
  bool explicitly_added = false;
};

struct NativeBookmarkSnapshot {
  std::vector<NativeBookmarkEntry> entries;
  // Only explicit model removal notifications are deletions. Missing rows in
  // an initial/incomplete snapshot or account-root teardown are not removals.
  std::vector<std::string> removed_keys;
  // Distinguishes live edits from a new adapter/model loaded from disk. This
  // local-only capture epoch never becomes a SyncRecord or user/device ID.
  std::string observation_session;
  // Local-only safety boundary: omitted unsupported content makes this an
  // incomplete observation, never permission for a partial merge or apply.
  bool local_data_blocked = false;
};

struct BookmarkNativeBinding {
  base::Uuid logical_id;
  std::string native_key;
  std::string apply_receipt;
};

struct BookmarkSyncProjection {
  std::vector<BookmarkRecord> records;
  std::vector<BookmarkNativeBinding> bindings;
  BookmarkSyncAuthorization authorization;
};

// Native keys are "local:<lowercase UUID>" or "account:<lowercase UUID>".
bool ParseNativeBookmarkKey(const std::string& key,
                            base::Uuid* uuid,
                            bool* account);
std::string NativeBookmarkKey(const base::Uuid& uuid, bool account);
base::Uuid InitialBookmarkSyncId(const std::string& native_key);

// Existing sibling keys survive insertions where possible. A lack of lexical
// space is explicit; callers can rebalance the affected sibling group only.
std::optional<std::string> BookmarkSortKeyBetween(
    const std::string& lower,
    const std::optional<std::string>& upper);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_BOOKMARK_SYNC_BRIDGE_TYPES_H_
