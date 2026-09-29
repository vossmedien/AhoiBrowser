// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_NATIVE_BOOKMARK_SYNC_ADAPTER_H_
#define AHOI_BROWSER_SYNC_NATIVE_BOOKMARK_SYNC_ADAPTER_H_

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ahoi/browser/sync/bookmark_sync_bridge_types.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service_observer.h"
#include "components/bookmarks/browser/bookmark_model_observer.h"

class BookmarkMergedSurfaceService;
namespace bookmarks {
class BookmarkModel;
}

namespace ahoi::sync {

// UI-sequence adapter; BookmarkModel remains mutation/persistence authority.
// It is instantiated only after local category consent, and never for OTR.
class NativeBookmarkSyncAdapter final
    : public bookmarks::BookmarkModelObserver,
      public BookmarkMergedSurfaceServiceObserver {
 public:
  using SnapshotCallback =
      base::RepeatingCallback<void(uint64_t, NativeBookmarkSnapshot)>;
  NativeBookmarkSyncAdapter(BookmarkMergedSurfaceService* service,
                            SnapshotCallback callback);
  ~NativeBookmarkSyncAdapter() override;

  uint64_t generation() const { return generation_; }
  bool ready() const;
  bool local_data_blocked() const { return local_data_blocked_; }
  void RequestCapture();
  // A reply to an older snapshot cannot overwrite more recent native edits.
  bool ApplyProjection(const BookmarkSyncProjection& projection,
                       uint64_t expected_generation);
  NativeBookmarkSnapshot Capture();
  void AcknowledgeCapture(uint64_t generation);

 private:
  void FlushCapture();
  std::string Key(const bookmarks::BookmarkNode* node) const;
  const bookmarks::BookmarkNode* Find(const std::string& key) const;
  void CaptureRemovedSubtree(const bookmarks::BookmarkNode* node);
  void RememberSubtree(const bookmarks::BookmarkNode* node);
  void StopObserving();

  void BookmarkModelLoaded(bool ids_reassigned) override;
  void BookmarkModelBeingDeleted() override;
  void OnWillMoveBookmarkNode(const bookmarks::BookmarkNode* old_parent,
                              size_t old_index,
                              const bookmarks::BookmarkNode* new_parent,
                              size_t new_index) override;
  void BookmarkNodeMoved(const bookmarks::BookmarkNode* old_parent,
                         size_t old_index,
                         const bookmarks::BookmarkNode* new_parent,
                         size_t new_index) override;
  void BookmarkNodeAdded(const bookmarks::BookmarkNode* parent,
                         size_t index,
                         bool added_by_user) override;
  void OnWillRemoveBookmarks(const bookmarks::BookmarkNode* parent,
                             size_t old_index,
                             const bookmarks::BookmarkNode* node,
                             const base::Location& location) override;
  void BookmarkNodeRemoved(const bookmarks::BookmarkNode* parent,
                           size_t old_index,
                           const bookmarks::BookmarkNode* node,
                           const std::set<GURL>& urls,
                           const base::Location& location) override;
  void BookmarkNodeChanged(const bookmarks::BookmarkNode* node) override;
  void BookmarkNodeFaviconChanged(const bookmarks::BookmarkNode* node) override;
  void BookmarkNodeChildrenReordered(
      const bookmarks::BookmarkNode* node) override;
  void OnWillRemoveAllUserBookmarks(const base::Location& location) override;
  void BookmarkAllUserNodesRemoved(const std::set<GURL>& urls,
                                   const base::Location& location) override;

  void BookmarkMergedSurfaceServiceLoaded() override;
  void BookmarkMergedSurfaceServiceBeingDeleted() override;
  void BookmarkNodeAdded(const BookmarkParentFolder& parent,
                         size_t index) override;
  void BookmarkNodesRemoved(
      const BookmarkParentFolder& parent,
      const base::flat_set<const bookmarks::BookmarkNode*>& nodes) override;
  void BookmarkNodeMoved(const BookmarkParentFolder& old_parent,
                         size_t old_index,
                         const BookmarkParentFolder& new_parent,
                         size_t new_index) override;
  void BookmarkParentFolderChildrenReordered(
      const BookmarkParentFolder& folder) override;
  void BookmarkAllUserNodesRemoved() override;
  void ExtensiveBookmarkChangesEnded() override;

  raw_ptr<BookmarkMergedSurfaceService> service_;
  raw_ptr<bookmarks::BookmarkModel> model_;
  SnapshotCallback callback_;
  std::map<int64_t, std::string> runtime_keys_;
  std::vector<std::string> removed_keys_;
  std::set<int64_t> explicitly_added_ids_;
  uint64_t generation_ = 0;
  bool capture_posted_ = false;
  bool applying_ = false;
  bool local_data_blocked_ = false;
  const std::string observation_session_ =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  base::WeakPtrFactory<NativeBookmarkSyncAdapter> weak_ptr_factory_{this};
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_NATIVE_BOOKMARK_SYNC_ADAPTER_H_
