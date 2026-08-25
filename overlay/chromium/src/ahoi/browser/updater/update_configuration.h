// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UPDATER_UPDATE_CONFIGURATION_H_
#define AHOI_BROWSER_UPDATER_UPDATE_CONFIGURATION_H_

#include <optional>
#include <string>

#include "ahoi/browser/updater/update_channel.h"

namespace ahoi::updater {

inline constexpr char kPinnedSparkleVersion[] = "2.9.6";

enum class ConfigurationError {
  kMissingChannel,
  kInvalidChannel,
  kMissingFeedUrl,
  kInsecureFeedUrl,
  kMissingPublicKey,
  kInvalidPublicKey,
  kFrameworkVersionMismatch,
  kUnsignedFeedAllowed,
  kVerifyAfterExtraction,
  kProfileSubmissionEnabled,
};

struct ConfigurationInput {
  std::string channel;
  std::string feed_url;
  std::string public_ed_key;
  std::string framework_version;
  bool require_signed_feed = false;
  bool verify_before_extraction = false;
  bool sends_system_profile = false;
};

struct UpdateConfiguration {
  UpdateChannel channel = UpdateChannel::kStable;
  std::string feed_url;
  std::string public_ed_key;
  std::string framework_version;
};

struct ConfigurationResult {
  std::optional<UpdateConfiguration> configuration;
  std::optional<ConfigurationError> error;

  bool ok() const { return configuration.has_value() && !error.has_value(); }
};

ConfigurationResult ValidateConfiguration(const ConfigurationInput& input);
std::string_view ConfigurationErrorName(ConfigurationError error);

}  // namespace ahoi::updater

#endif  // AHOI_BROWSER_UPDATER_UPDATE_CONFIGURATION_H_
