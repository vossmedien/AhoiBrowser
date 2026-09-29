// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SAVED_ACCOUNT_MENU_H_
#define AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SAVED_ACCOUNT_MENU_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ahoi {

// Returns the positions in |saved_usernames|, in their order, that the HTTP
// auth dialog offers in its saved-account menu while the username field
// contains |typed_username|.
//
// The dialog prefills the preferred account and keeps the name after a
// failed attempt, so a field that names a saved account must not hide the
// others: when |typed_username| is empty or names one saved account, every
// account is offered. Otherwise the menu narrows to the accounts that start
// with the typed text. Both comparisons ignore case and surrounding
// whitespace; they only decide what the menu lists. Choosing an account and
// filling its password still require the exact saved username.
std::vector<size_t> SelectSavedAccountMenuEntries(
    const std::vector<std::u16string_view>& saved_usernames,
    std::u16string_view typed_username);

}  // namespace ahoi

#endif  // AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SAVED_ACCOUNT_MENU_H_
