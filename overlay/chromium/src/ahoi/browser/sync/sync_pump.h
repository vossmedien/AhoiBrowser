// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SYNC_PUMP_H_
#define AHOI_BROWSER_SYNC_SYNC_PUMP_H_

#include <cstddef>
#include <string>
#include <vector>

#include "ahoi/browser/sync/sync_model.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"

namespace ahoi::sync {

class SyncProvider;
class SyncStore;

// Sequence-affine transport state machine between the local-first SyncStore
// and one asynchronous provider. The store remains canonical: uploads are
// acknowledged by mutation id, downloads commit atomically with their token,
// and a provider failure can never discard an outbox row.
class SyncPump final {
 public:
  struct Options {
    size_t upload_batch_size = 100;
    base::TimeDelta initial_retry_delay = base::Seconds(5);
    base::TimeDelta maximum_retry_delay = base::Hours(1);
  };

  using CompletionCallback =
      base::OnceCallback<void(bool success, std::string safe_error)>;

  SyncPump(SyncStore* store, SyncProvider* provider);
  SyncPump(SyncStore* store, SyncProvider* provider, Options options);
  SyncPump(const SyncPump&) = delete;
  SyncPump& operator=(const SyncPump&) = delete;
  ~SyncPump();

  // Coalesces a request received during an active cycle and runs it before
  // completing callers. Returns false only when this object cannot start.
  bool SyncNow(CompletionCallback callback);
  void Cancel();

  bool syncing_for_testing() const { return syncing_; }

 private:
  void StartCycle();
  void UploadNextPage();
  void OnUploadFinished(std::vector<SyncChange> attempted,
                        bool success,
                        std::vector<std::string> acknowledged_ids,
                        std::string error);
  void DownloadNextPage(std::string requested_token);
  void OnDownloadFinished(std::string requested_token,
                          bool success,
                          ProviderBatch batch,
                          std::string error);
  void FinishSuccess();
  void FinishFailure(std::string error);
  base::TimeDelta NextRetryDelay() const;
  void RunCallbacks(bool success, const std::string& safe_error);

  const raw_ptr<SyncStore> store_;
  const raw_ptr<SyncProvider> provider_;
  const Options options_;
  const scoped_refptr<base::SequencedTaskRunner> task_runner_;
  std::vector<CompletionCallback> callbacks_;
  bool syncing_ = false;
  bool cycle_requested_ = false;
  base::WeakPtrFactory<SyncPump> weak_ptr_factory_{this};
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_PUMP_H_
