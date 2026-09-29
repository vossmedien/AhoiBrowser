// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SYNC_AUTHORIZATION_H_
#define AHOI_BROWSER_SYNC_SYNC_AUTHORIZATION_H_

#include "base/functional/callback.h"
#include "base/uuid.h"

namespace ahoi::sync {

// Thread-safe validity check bound to the original local/account generation.
// It carries no wire value or browser pointer and must stay revoked after a
// later reapproval. Empty means unauthorized at asynchronous write boundaries.
using SyncAuthorization = base::RepeatingCallback<bool()>;
// Captures a permanently revocable, record-specific preference approval.
// Empty source/lease means denied; it is local metadata, never serialized.
using SettingAuthorizationSource =
    base::RepeatingCallback<SyncAuthorization(const base::Uuid&)>;

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_AUTHORIZATION_H_
