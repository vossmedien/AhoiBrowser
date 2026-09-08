// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/browser_setting_consent.h"

#include "base/functional/bind.h"

namespace ahoi::sync {

BrowserSettingConsent::BrowserSettingConsent() = default;
BrowserSettingConsent::~BrowserSettingConsent() {
  SetAllowed({});
}

void BrowserSettingConsent::SetAllowed(const std::set<base::Uuid>& ids) {
  base::AutoLock guard(lock_);
  for (auto it = cancelled_.begin(); it != cancelled_.end();) {
    if (!ids.contains(it->first)) {
      it->second->store(true, std::memory_order_release);
      it = cancelled_.erase(it);
    } else {
      ++it;
    }
  }
  for (const auto& id : ids) {
    if (!cancelled_.contains(id)) {
      cancelled_.emplace(id, std::make_shared<std::atomic<bool>>(false));
    }
  }
}

SyncAuthorization BrowserSettingConsent::Capture(const base::Uuid& id) {
  base::AutoLock guard(lock_);
  const auto found = cancelled_.find(id);
  if (found == cancelled_.end()) {
    return {};
  }
  return base::BindRepeating(
      [](std::shared_ptr<std::atomic<bool>> cancelled) {
        return !cancelled->load(std::memory_order_acquire);
      },
      found->second);
}

}  // namespace ahoi::sync
