// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_TEXT_CODEC_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_TEXT_CODEC_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"

namespace ahoi {

enum class DeveloperHeaderTextError {
  kNone,
  kMissingSeparator,
  kInvalidName,
  kInvalidValue,
  kDuplicateName,
  kTooManyRules,
  kTooLarge,
};

struct DeveloperHeaderTextParseResult {
  std::vector<DeveloperHeaderRule> rules;
  DeveloperHeaderTextError error = DeveloperHeaderTextError::kNone;
  size_t error_line = 0;

  bool succeeded() const { return error == DeveloperHeaderTextError::kNone; }
};

// Human-editable format used by the native profile editor:
//   Header-Name: value
//   -Header-To-Remove
// Empty lines and lines whose first non-space character is '#' are ignored.
DeveloperHeaderTextParseResult ParseDeveloperHeaderRules(std::string_view text);
std::string FormatDeveloperHeaderRules(
    const std::vector<DeveloperHeaderRule>& rules);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_TEXT_CODEC_H_
