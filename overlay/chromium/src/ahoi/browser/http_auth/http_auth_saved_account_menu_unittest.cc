// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_saved_account_menu.h"

#include <string_view>
#include <vector>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

const std::vector<std::u16string_view>& Accounts() {
  static const std::vector<std::u16string_view> kAccounts = {u"alice", u"bob",
                                                             u"albert"};
  return kAccounts;
}

TEST(HttpAuthSavedAccountMenuTest, EmptyFieldOffersEveryAccount) {
  EXPECT_THAT(SelectSavedAccountMenuEntries(Accounts(), u""),
              ElementsAre(0u, 1u, 2u));
  EXPECT_THAT(SelectSavedAccountMenuEntries(Accounts(), u"  \t"),
              ElementsAre(0u, 1u, 2u));
}

TEST(HttpAuthSavedAccountMenuTest, PrefilledAccountKeepsFullList) {
  // The preferred account is prefilled; the others stay selectable.
  EXPECT_THAT(SelectSavedAccountMenuEntries(Accounts(), u"alice"),
              ElementsAre(0u, 1u, 2u));
  // After choosing another account from the menu the list stays complete.
  EXPECT_THAT(SelectSavedAccountMenuEntries(Accounts(), u"bob"),
              ElementsAre(0u, 1u, 2u));
}

TEST(HttpAuthSavedAccountMenuTest, SavedNameIgnoresCaseAndWhitespace) {
  // A failed attempt keeps the typed name, which may differ in case or carry
  // stray whitespace.
  EXPECT_THAT(SelectSavedAccountMenuEntries(Accounts(), u"ALICE"),
              ElementsAre(0u, 1u, 2u));
  EXPECT_THAT(SelectSavedAccountMenuEntries(Accounts(), u" Bob "),
              ElementsAre(0u, 1u, 2u));
}

TEST(HttpAuthSavedAccountMenuTest, PartialNameFiltersByPrefix) {
  EXPECT_THAT(SelectSavedAccountMenuEntries(Accounts(), u"al"),
              ElementsAre(0u, 2u));
  EXPECT_THAT(SelectSavedAccountMenuEntries(Accounts(), u"B"), ElementsAre(1u));
  EXPECT_THAT(SelectSavedAccountMenuEntries(Accounts(), u" alb"),
              ElementsAre(2u));
}

TEST(HttpAuthSavedAccountMenuTest, UnknownNameOffersNothing) {
  // A new username being typed does not pop up unrelated accounts.
  EXPECT_THAT(SelectSavedAccountMenuEntries(Accounts(), u"carol"), IsEmpty());
  EXPECT_THAT(SelectSavedAccountMenuEntries(Accounts(), u"alicex"), IsEmpty());
}

TEST(HttpAuthSavedAccountMenuTest, SavedNameThatPrefixesAnotherKeepsAll) {
  const std::vector<std::u16string_view> accounts = {u"al", u"alice", u"bob"};
  EXPECT_THAT(SelectSavedAccountMenuEntries(accounts, u"al"),
              ElementsAre(0u, 1u, 2u));
  EXPECT_THAT(SelectSavedAccountMenuEntries(accounts, u"ali"), ElementsAre(1u));
}

TEST(HttpAuthSavedAccountMenuTest, NonAsciiNamesFoldCase) {
  const std::vector<std::u16string_view> accounts = {u"j\u00fcrgen",
                                                     u"\u00d6mer"};
  EXPECT_THAT(SelectSavedAccountMenuEntries(accounts, u"J\u00dcR"),
              ElementsAre(0u));
  EXPECT_THAT(SelectSavedAccountMenuEntries(accounts, u"\u00f6mer"),
              ElementsAre(0u, 1u));
}

TEST(HttpAuthSavedAccountMenuTest, NoSavedAccountsOffersNothing) {
  EXPECT_THAT(SelectSavedAccountMenuEntries({}, u""), IsEmpty());
  EXPECT_THAT(SelectSavedAccountMenuEntries({}, u"alice"), IsEmpty());
}

}  // namespace
}  // namespace ahoi
