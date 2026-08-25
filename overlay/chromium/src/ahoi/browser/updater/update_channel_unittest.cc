// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/updater/update_channel.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::updater {
namespace {

TEST(UpdateChannelTest, ParsesOnlyReleaseChannels) {
  EXPECT_EQ(UpdateChannel::kStable, ParseUpdateChannel("stable"));
  EXPECT_EQ(UpdateChannel::kBeta, ParseUpdateChannel("beta"));
  EXPECT_EQ(UpdateChannel::kNightly, ParseUpdateChannel("nightly"));
  EXPECT_FALSE(ParseUpdateChannel("dogfood"));
  EXPECT_FALSE(ParseUpdateChannel("Stable"));
}

TEST(UpdateChannelTest, UsesMonotonicChannelVisibility) {
  EXPECT_TRUE(AllowedSparkleChannels(UpdateChannel::kStable).empty());
  EXPECT_EQ((std::vector<std::string_view>{"beta"}),
            AllowedSparkleChannels(UpdateChannel::kBeta));
  EXPECT_EQ((std::vector<std::string_view>{"beta", "nightly"}),
            AllowedSparkleChannels(UpdateChannel::kNightly));
}

}  // namespace
}  // namespace ahoi::updater
