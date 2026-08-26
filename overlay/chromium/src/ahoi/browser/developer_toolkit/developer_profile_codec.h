// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_CODEC_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_CODEC_H_

#include <optional>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "base/values.h"

namespace ahoi {

// Encodes one already validated profile. The store owns schema/version and
// origin-key handling; this codec deliberately has no logging or I/O.
std::optional<base::DictValue> SerializeDeveloperProfile(
    const DeveloperProfile& profile);

// Strictly decodes one profile dictionary. Unknown fields are ignored for
// forwards compatibility, while malformed known fields fail closed.
std::optional<DeveloperProfile> DeserializeDeveloperProfile(
    const base::DictValue& value,
    const url::Origin* owner_origin = nullptr);

// Produces a payload containing only individually opted-in developer assets
// and header profiles. Secret references and their values are always omitted.
std::optional<base::DictValue> SerializeDeveloperProfileForSync(
    const DeveloperProfile& profile);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_CODEC_H_
