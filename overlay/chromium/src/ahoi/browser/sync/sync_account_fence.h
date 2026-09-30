// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SYNC_ACCOUNT_FENCE_H_
#define AHOI_BROWSER_SYNC_SYNC_ACCOUNT_FENCE_H_

#include <atomic>
#include <cstdint>

namespace ahoi::sync {

// Decides what an account-change notification means for a verified key
// lease. CloudKit posts CKAccountChangedNotification for token refreshes,
// wake and iCloud daemon restarts, not only for a real sign-out or account
// switch (29/30 Sep 2026: five notifications overnight, same account).
//
// Close() fences the lease at once, on any thread, so no delayed upload can
// reach a possibly different account. The owner then re-verifies the account
// and reports the verdict; only the newest outstanding check may reopen the
// lease, and only when it saw the originally verified user record.
class SyncAccountFence {
 public:
  enum class Verdict {
    kSameAccount,   // Available, same user record ID as the verified one.
    kOtherAccount,  // Available, but a different user record ID.
    kUnavailable,   // Signed out, restricted or no account.
    kError,         // The check itself failed (network, daemon, timeout).
  };
  enum class Action {
    kIgnore,  // A newer notification is outstanding; its check decides.
    kReopen,  // The lease is open again.
    kRevoke,  // A real account change: revoke the lease permanently.
    kRetry,   // Still fenced; check again later.
  };

  SyncAccountFence() = default;
  SyncAccountFence(const SyncAccountFence&) = delete;
  SyncAccountFence& operator=(const SyncAccountFence&) = delete;

  // Any thread. Closes the fence and returns the id of the check that must
  // now run. Every later Close() supersedes earlier checks.
  uint64_t Close();
  // Owner sequence. A finished check for `check`.
  Action OnVerified(uint64_t check, Verdict verdict);
  // Any thread. True while no check is outstanding.
  bool open() const;

 private:
  std::atomic<uint64_t> last_check_{0};
  // 0 when open, otherwise the newest outstanding check id.
  std::atomic<uint64_t> outstanding_{0};
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_ACCOUNT_FENCE_H_
