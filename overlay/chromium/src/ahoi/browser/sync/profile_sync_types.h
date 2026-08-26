// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_PROFILE_SYNC_TYPES_H_
#define AHOI_BROWSER_SYNC_PROFILE_SYNC_TYPES_H_

#include <optional>
#include <string>

#include "base/uuid.h"

namespace ahoi::sync {

// UI-thread descriptor for one ordinary local tab. It intentionally excludes
// cookies, credentials, page state and incognito metadata. Keeping this type
// independent of ProfileSyncService lets the blocking sync backend remain a
// UI-free layer.
struct LocalTabState {
  std::string stable_key;
  base::Uuid sync_id;
  std::optional<base::Uuid> workspace_id;
  std::string url;
  std::string title;
  bool pinned = false;
  bool active = false;

  friend bool operator==(const LocalTabState&, const LocalTabState&) = default;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_PROFILE_SYNC_TYPES_H_
