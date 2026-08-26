// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_pump.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

#include "ahoi/browser/sync/sync_provider.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/task/bind_post_task.h"

namespace ahoi::sync {
namespace {

std::string SafeProviderError(std::string error) {
  // Provider implementations return categories, never raw CKError text or
  // payloads. Fail closed if a future provider violates that boundary.
  static constexpr const char* kAllowedErrors[] = {
      "account_unavailable", "cancelled",      "network",
      "provider_error",      "quota",          "rate_limited",
      "server",              "temporarily_unavailable"};
  base::TrimWhitespaceASCII(error, base::TRIM_ALL, &error);
  for (const char* allowed : kAllowedErrors) {
    if (error == allowed) {
      return error;
    }
  }
  return "provider_error";
}

}  // namespace

SyncPump::SyncPump(SyncStore* store, SyncProvider* provider)
    : SyncPump(store, provider, Options()) {}

SyncPump::SyncPump(SyncStore* store,
                   SyncProvider* provider,
                   Options options)
    : store_(store),
      provider_(provider),
      options_(std::move(options)),
      task_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {
  CHECK(store_);
  CHECK(provider_);
  CHECK_GT(options_.upload_batch_size, 0u);
  CHECK_GT(options_.initial_retry_delay, base::TimeDelta());
  CHECK_GE(options_.maximum_retry_delay, options_.initial_retry_delay);
}

SyncPump::~SyncPump() {
  Cancel();
}

bool SyncPump::SyncNow(CompletionCallback callback) {
  if (!store_ || !provider_ || !task_runner_) {
    return false;
  }
  if (callback) {
    callbacks_.push_back(std::move(callback));
  }
  if (syncing_) {
    cycle_requested_ = true;
    return true;
  }
  syncing_ = true;
  StartCycle();
  return true;
}

void SyncPump::Cancel() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  syncing_ = false;
  cycle_requested_ = false;
  RunCallbacks(false, "cancelled");
}

void SyncPump::StartCycle() {
  cycle_requested_ = false;
  const RetryState retry = store_->GetRetryState();
  if (!retry.next_attempt.is_null() && retry.next_attempt > base::Time::Now()) {
    FinishFailure(retry.last_error.empty() ? "temporarily_unavailable"
                                           : retry.last_error);
    return;
  }
  UploadNextPage();
}

void SyncPump::UploadNextPage() {
  std::vector<SyncChange> changes;
  if (store_->ReadOutbox(options_.upload_batch_size, &changes) !=
      SyncStore::Result::kOk) {
    FinishFailure("provider_error");
    return;
  }
  if (changes.empty()) {
    DownloadNextPage(store_->GetChangeToken());
    return;
  }

  std::vector<SyncChange> attempted = changes;
  provider_->Upload(
      std::move(changes),
      base::BindPostTask(
          task_runner_,
          base::BindOnce(&SyncPump::OnUploadFinished,
                         weak_ptr_factory_.GetWeakPtr(),
                         std::move(attempted))));
}

void SyncPump::OnUploadFinished(
    std::vector<SyncChange> attempted,
    bool success,
    std::vector<std::string> acknowledged_ids,
    std::string error) {
  if (!success) {
    FinishFailure(SafeProviderError(std::move(error)));
    return;
  }

  std::set<std::string> expected;
  for (const SyncChange& change : attempted) {
    expected.insert(change.mutation_id);
  }
  std::set<std::string> unique_acknowledgements;
  for (const std::string& id : acknowledged_ids) {
    if (!expected.contains(id) || !unique_acknowledgements.insert(id).second) {
      FinishFailure("provider_error");
      return;
    }
  }
  if (unique_acknowledgements.empty()) {
    FinishFailure("provider_error");
    return;
  }

  acknowledged_ids.assign(unique_acknowledgements.begin(),
                          unique_acknowledgements.end());
  if (store_->AcknowledgeOutbox(acknowledged_ids) !=
      SyncStore::Result::kOk) {
    FinishFailure("provider_error");
    return;
  }
  UploadNextPage();
}

void SyncPump::DownloadNextPage(std::string requested_token) {
  provider_->Download(
      requested_token,
      base::BindPostTask(
          task_runner_,
          base::BindOnce(&SyncPump::OnDownloadFinished,
                         weak_ptr_factory_.GetWeakPtr(), requested_token)));
}

void SyncPump::OnDownloadFinished(std::string requested_token,
                                  bool success,
                                  ProviderBatch batch,
                                  std::string error) {
  if (!success) {
    FinishFailure(SafeProviderError(std::move(error)));
    return;
  }
  if (batch.has_more && batch.next_change_token == requested_token) {
    FinishFailure("provider_error");
    return;
  }
  if (store_->ApplyRemoteBatch(batch) != SyncStore::Result::kOk) {
    FinishFailure("provider_error");
    return;
  }
  if (batch.has_more) {
    DownloadNextPage(std::move(batch.next_change_token));
    return;
  }
  if (cycle_requested_) {
    StartCycle();
    return;
  }
  FinishSuccess();
}

void SyncPump::FinishSuccess() {
  std::ignore = store_->ClearRetry();
  syncing_ = false;
  RunCallbacks(true, std::string());
}

void SyncPump::FinishFailure(std::string error) {
  error = SafeProviderError(std::move(error));
  std::ignore = store_->MarkRetry(base::Time::Now() + NextRetryDelay(), error);
  syncing_ = false;
  cycle_requested_ = false;
  RunCallbacks(false, error);
}

base::TimeDelta SyncPump::NextRetryDelay() const {
  base::TimeDelta delay = options_.initial_retry_delay;
  const int attempts = std::max(0, store_->GetRetryState().attempt);
  for (int index = 0;
       index < attempts && delay < options_.maximum_retry_delay; ++index) {
    delay = std::min(delay * 2, options_.maximum_retry_delay);
  }
  return delay;
}

void SyncPump::RunCallbacks(bool success, const std::string& safe_error) {
  std::vector<CompletionCallback> callbacks = std::move(callbacks_);
  callbacks_.clear();
  for (CompletionCallback& callback : callbacks) {
    if (callback) {
      std::move(callback).Run(success, safe_error);
    }
  }
}

}  // namespace ahoi::sync
