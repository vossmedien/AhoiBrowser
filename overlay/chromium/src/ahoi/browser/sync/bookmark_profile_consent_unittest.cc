// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

class BookmarkProfileConsentTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment environment_;
};

TEST_F(BookmarkProfileConsentTest, CategoryApprovalCannotEnableGlobalSync) {
  TestingProfile profile;
  ProfileSyncService service(&profile);
  EXPECT_FALSE(service.bookmark_sync_enabled());
  EXPECT_FALSE(service.sync_enabled());
  ASSERT_TRUE(service.SetBookmarkSyncEnabled(true));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(service.bookmark_sync_enabled());
  EXPECT_FALSE(service.sync_enabled());
  EXPECT_FALSE(service.initialized());
  EXPECT_FALSE(base::PathExists(
      profile.GetPath().AppendASCII("Ahoi Sync").AppendASCII("sync.sqlite")));
  ASSERT_TRUE(service.SetBookmarkSyncEnabled(false));
  EXPECT_FALSE(service.bookmark_sync_enabled());
  service.Shutdown();
}

TEST_F(BookmarkProfileConsentTest, LegacyGlobalOptInLeavesCategoryDefaultOff) {
  TestingProfile profile;
  profile.GetPrefs()->SetBoolean(kSyncEnabledPref, true);
  ProfileSyncService service(&profile);
  EXPECT_TRUE(service.sync_enabled());
  EXPECT_FALSE(service.bookmark_sync_enabled());
  EXPECT_TRUE(profile.GetPrefs()
                  ->FindPreference(kBookmarkSyncEnabledPref)
                  ->IsDefaultValue());
  service.Shutdown();
  base::RunLoop().RunUntilIdle();
}

}  // namespace
}  // namespace ahoi::sync
