// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/cloudkit_sync_provider_mac.h"

#include <memory>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {

class CloudKitSyncProviderMacTest : public testing::Test {
 protected:
  std::unique_ptr<CloudKitSyncProviderMac> CreateProvider(
      const base::FilePath& state_path) {
    return CloudKitSyncProviderMac::CreateForTesting(state_path);
  }

  base::RepeatingCallback<bool()> MakeDelayedCacheWrite(
      CloudKitSyncProviderMac& provider) {
    return provider.MakeDelayedCacheWriteForTesting();
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(CloudKitSyncProviderMacTest,
       LateCacheCallbackCannotPersistAfterProviderShutdown) {
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  const base::FilePath state_path =
      temp_dir_.GetPath().AppendASCII("cksync.state");
  const base::FilePath inbox_path = state_path.AddExtensionASCII("inbox");
  std::unique_ptr<CloudKitSyncProviderMac> provider =
      CreateProvider(state_path);
  base::RepeatingCallback<bool()> delayed_cache_write =
      MakeDelayedCacheWrite(*provider);

  ASSERT_TRUE(delayed_cache_write.Run());
  ASSERT_TRUE(base::PathExists(inbox_path));
  ASSERT_TRUE(base::DeleteFile(inbox_path));

  provider.reset();

  EXPECT_FALSE(delayed_cache_write.Run());
  EXPECT_FALSE(base::PathExists(inbox_path));
}

}  // namespace ahoi::sync
