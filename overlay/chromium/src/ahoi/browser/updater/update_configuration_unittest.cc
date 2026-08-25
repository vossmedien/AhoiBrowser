// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/updater/update_configuration.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::updater {
namespace {

ConfigurationInput ValidInput() {
  return {
      .channel = "stable",
      .feed_url = "https://updates.example.invalid/appcast.xml",
      .public_ed_key = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
      .framework_version = kPinnedSparkleVersion,
      .require_signed_feed = true,
      .verify_before_extraction = true,
      .sends_system_profile = false,
  };
}

TEST(UpdateConfigurationTest, AcceptsHardenedPinnedConfiguration) {
  const ConfigurationResult result = ValidateConfiguration(ValidInput());
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(UpdateChannel::kStable, result.configuration->channel);
}

TEST(UpdateConfigurationTest, RejectsUnknownChannel) {
  ConfigurationInput input = ValidInput();
  input.channel = "dogfood";
  const ConfigurationResult result = ValidateConfiguration(input);
  EXPECT_EQ(ConfigurationError::kInvalidChannel, result.error);
}

TEST(UpdateConfigurationTest, RejectsInsecureOrCredentialedFeed) {
  for (const char* feed : {"http://updates.example.invalid/appcast.xml",
                           "https://user:secret@updates.example.invalid/feed",
                           "https://updates.example.invalid/feed#fragment"}) {
    ConfigurationInput input = ValidInput();
    input.feed_url = feed;
    EXPECT_EQ(ConfigurationError::kInsecureFeedUrl,
              ValidateConfiguration(input).error);
  }
}

TEST(UpdateConfigurationTest, RejectsMissingOrMalformedEd25519Key) {
  ConfigurationInput missing = ValidInput();
  missing.public_ed_key.clear();
  EXPECT_EQ(ConfigurationError::kMissingPublicKey,
            ValidateConfiguration(missing).error);

  ConfigurationInput malformed = ValidInput();
  malformed.public_ed_key = "dG9vIHNob3J0";
  EXPECT_EQ(ConfigurationError::kInvalidPublicKey,
            ValidateConfiguration(malformed).error);
}

TEST(UpdateConfigurationTest, RejectsWrongFrameworkAndWeakenedSecurity) {
  ConfigurationInput wrong_version = ValidInput();
  wrong_version.framework_version = "2.9.5";
  EXPECT_EQ(ConfigurationError::kFrameworkVersionMismatch,
            ValidateConfiguration(wrong_version).error);

  ConfigurationInput unsigned_feed = ValidInput();
  unsigned_feed.require_signed_feed = false;
  EXPECT_EQ(ConfigurationError::kUnsignedFeedAllowed,
            ValidateConfiguration(unsigned_feed).error);

  ConfigurationInput late_verification = ValidInput();
  late_verification.verify_before_extraction = false;
  EXPECT_EQ(ConfigurationError::kVerifyAfterExtraction,
            ValidateConfiguration(late_verification).error);

  ConfigurationInput telemetry = ValidInput();
  telemetry.sends_system_profile = true;
  EXPECT_EQ(ConfigurationError::kProfileSubmissionEnabled,
            ValidateConfiguration(telemetry).error);
}

}  // namespace
}  // namespace ahoi::updater
