// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_saved_account_menu.h"

#include <algorithm>

#include "base/i18n/case_conversion.h"
#include "base/strings/string_util.h"

namespace ahoi {

namespace {

std::u16string NormalizeForMenu(std::u16string_view username) {
  return base::i18n::FoldCase(base::TrimWhitespace(username, base::TRIM_ALL));
}

}  // namespace

std::vector<size_t> SelectSavedAccountMenuEntries(
    const std::vector<std::u16string_view>& saved_usernames,
    std::u16string_view typed_username) {
  const std::u16string typed = NormalizeForMenu(typed_username);
  std::vector<std::u16string> normalized;
  normalized.reserve(saved_usernames.size());
  for (std::u16string_view username : saved_usernames) {
    normalized.push_back(NormalizeForMenu(username));
  }

  const bool offer_all =
      typed.empty() || std::ranges::find(normalized, typed) != normalized.end();
  std::vector<size_t> entries;
  entries.reserve(saved_usernames.size());
  for (size_t i = 0; i < normalized.size(); ++i) {
    if (offer_all || normalized[i].starts_with(typed)) {
      entries.push_back(i);
    }
  }
  return entries;
}

}  // namespace ahoi
