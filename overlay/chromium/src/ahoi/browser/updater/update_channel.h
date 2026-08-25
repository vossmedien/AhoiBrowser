// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UPDATER_UPDATE_CHANNEL_H_
#define AHOI_BROWSER_UPDATER_UPDATE_CHANNEL_H_

#include <optional>
#include <string_view>
#include <vector>

namespace ahoi::updater {

enum class UpdateChannel {
  kStable,
  kBeta,
  kNightly,
};

std::optional<UpdateChannel> ParseUpdateChannel(std::string_view value);
std::string_view UpdateChannelName(UpdateChannel channel);

// Sparkle always includes untagged stable updates. Beta builds additionally
// accept beta items; nightly builds accept beta and nightly items so a newer
// security release can supersede a nightly build without changing feeds.
std::vector<std::string_view> AllowedSparkleChannels(UpdateChannel channel);

}  // namespace ahoi::updater

#endif  // AHOI_BROWSER_UPDATER_UPDATE_CHANNEL_H_
