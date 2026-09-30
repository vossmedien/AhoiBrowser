// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_account_fence.h"

namespace ahoi::sync {

uint64_t SyncAccountFence::Close() {
  const uint64_t check =
      last_check_.fetch_add(1, std::memory_order_acq_rel) + 1;
  // Keep the newest id even when two notifications race on two threads.
  uint64_t current = outstanding_.load(std::memory_order_acquire);
  while (current < check &&
         !outstanding_.compare_exchange_weak(current, check,
                                             std::memory_order_acq_rel)) {
  }
  return check;
}

SyncAccountFence::Action SyncAccountFence::OnVerified(uint64_t check,
                                                      Verdict verdict) {
  if (check == 0 ||
      outstanding_.load(std::memory_order_acquire) != check) {
    return Action::kIgnore;
  }
  switch (verdict) {
    case Verdict::kSameAccount: {
      // Reopen only if no newer notification arrived meanwhile.
      uint64_t expected = check;
      return outstanding_.compare_exchange_strong(expected, 0,
                                                  std::memory_order_acq_rel)
                 ? Action::kReopen
                 : Action::kIgnore;
    }
    case Verdict::kOtherAccount:
    case Verdict::kUnavailable:
      return Action::kRevoke;
    case Verdict::kError:
      return Action::kRetry;
  }
  return Action::kRetry;
}

bool SyncAccountFence::open() const {
  return outstanding_.load(std::memory_order_acquire) == 0;
}

}  // namespace ahoi::sync
