// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SYNC_UNIFIED_VALIDATION_H_
#define AHOI_BROWSER_SYNC_SYNC_UNIFIED_VALIDATION_H_

#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync {

base::Uuid CapabilityIdForDevice(const base::Uuid& device_id);
bool ValidateCapability(const DeviceCapabilityRecord& record);
bool ValidateSharedTarget(const std::string& url,
                          const std::optional<SharedTabTargetKind>& kind,
                          const std::optional<std::string>& local_scheme);
bool SharedPresenceMatchesPage(const RemoteTabRecord& presence,
                               const TreeNodeRecord& page);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_UNIFIED_VALIDATION_H_
