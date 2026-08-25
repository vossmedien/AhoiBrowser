// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/updater/update_configuration.h"

#include <utility>

#include "base/base64.h"
#include "url/gurl.h"

namespace ahoi::updater {
namespace {

ConfigurationResult Failure(ConfigurationError error) {
  return {.configuration = std::nullopt, .error = error};
}

bool IsCredentialFreeHttpsUrl(const std::string& value) {
  const GURL url(value);
  return url.is_valid() && url.SchemeIs("https") && url.has_host() &&
         !url.has_username() && !url.has_password() && !url.has_ref();
}

}  // namespace

ConfigurationResult ValidateConfiguration(const ConfigurationInput& input) {
  if (input.channel.empty()) {
    return Failure(ConfigurationError::kMissingChannel);
  }
  const std::optional<UpdateChannel> channel =
      ParseUpdateChannel(input.channel);
  if (!channel) {
    return Failure(ConfigurationError::kInvalidChannel);
  }
  if (input.feed_url.empty()) {
    return Failure(ConfigurationError::kMissingFeedUrl);
  }
  if (!IsCredentialFreeHttpsUrl(input.feed_url)) {
    return Failure(ConfigurationError::kInsecureFeedUrl);
  }
  if (input.public_ed_key.empty()) {
    return Failure(ConfigurationError::kMissingPublicKey);
  }
  std::string decoded_key;
  if (!base::Base64Decode(input.public_ed_key, &decoded_key) ||
      decoded_key.size() != 32) {
    return Failure(ConfigurationError::kInvalidPublicKey);
  }
  if (input.framework_version != kPinnedSparkleVersion) {
    return Failure(ConfigurationError::kFrameworkVersionMismatch);
  }
  if (!input.require_signed_feed) {
    return Failure(ConfigurationError::kUnsignedFeedAllowed);
  }
  if (!input.verify_before_extraction) {
    return Failure(ConfigurationError::kVerifyAfterExtraction);
  }
  if (input.sends_system_profile) {
    return Failure(ConfigurationError::kProfileSubmissionEnabled);
  }
  return {
      .configuration =
          UpdateConfiguration{
              .channel = *channel,
              .feed_url = input.feed_url,
              .public_ed_key = input.public_ed_key,
              .framework_version = input.framework_version,
          },
      .error = std::nullopt,
  };
}

std::string_view ConfigurationErrorName(ConfigurationError error) {
  switch (error) {
    case ConfigurationError::kMissingChannel:
      return "missing-channel";
    case ConfigurationError::kInvalidChannel:
      return "invalid-channel";
    case ConfigurationError::kMissingFeedUrl:
      return "missing-feed-url";
    case ConfigurationError::kInsecureFeedUrl:
      return "insecure-feed-url";
    case ConfigurationError::kMissingPublicKey:
      return "missing-public-key";
    case ConfigurationError::kInvalidPublicKey:
      return "invalid-public-key";
    case ConfigurationError::kFrameworkVersionMismatch:
      return "framework-version-mismatch";
    case ConfigurationError::kUnsignedFeedAllowed:
      return "signed-feed-not-required";
    case ConfigurationError::kVerifyAfterExtraction:
      return "verification-before-extraction-disabled";
    case ConfigurationError::kProfileSubmissionEnabled:
      return "profile-submission-enabled";
  }
  return "unknown-configuration-error";
}

}  // namespace ahoi::updater
