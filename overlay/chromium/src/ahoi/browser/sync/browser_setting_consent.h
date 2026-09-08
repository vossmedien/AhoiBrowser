// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_BROWSER_SETTING_CONSENT_H_
#define AHOI_BROWSER_SYNC_BROWSER_SETTING_CONSENT_H_

#include <atomic>
#include <map>
#include <memory>
#include <set>

#include "ahoi/browser/sync/sync_authorization.h"
#include "base/synchronization/lock.h"

namespace ahoi::sync {

// UI opt-out revokes an outstanding provider lease synchronously. Reapproval
// creates a new lease; it cannot resurrect a cancelled in-flight upload.
class BrowserSettingConsent {
 public:
  BrowserSettingConsent();
  ~BrowserSettingConsent();
  void SetAllowed(const std::set<base::Uuid>& ids);
  SyncAuthorization Capture(const base::Uuid& id);

 private:
  base::Lock lock_;
  std::map<base::Uuid, std::shared_ptr<std::atomic<bool>>> cancelled_
      GUARDED_BY(lock_);
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_BROWSER_SETTING_CONSENT_H_
