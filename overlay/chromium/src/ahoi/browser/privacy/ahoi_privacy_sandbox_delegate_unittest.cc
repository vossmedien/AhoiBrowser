// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/ahoi_privacy_sandbox_delegate.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::privacy {
namespace {

TEST(AhoiPrivacySandboxDelegateTest, AlwaysFailsClosed) {
  AhoiPrivacySandboxDelegate regular(false);
  EXPECT_TRUE(regular.IsPrivacySandboxRestricted());
  EXPECT_FALSE(regular.IsPrivacySandboxCurrentlyUnrestricted());
  EXPECT_FALSE(regular.IsIncognitoProfile());
  EXPECT_FALSE(regular.HasAppropriateTopicsConsent());
  EXPECT_FALSE(regular.IsSubjectToM1NoticeRestricted());
  EXPECT_FALSE(regular.IsRestrictedNoticeEnabled());

  AhoiPrivacySandboxDelegate incognito(true);
  EXPECT_TRUE(incognito.IsPrivacySandboxRestricted());
  EXPECT_TRUE(incognito.IsIncognitoProfile());
}

}  // namespace
}  // namespace ahoi::privacy
