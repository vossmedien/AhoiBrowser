// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/updater/update_channel.h"

namespace ahoi::updater {

std::optional<UpdateChannel> ParseUpdateChannel(std::string_view value) {
  if (value == "stable") {
    return UpdateChannel::kStable;
  }
  if (value == "beta") {
    return UpdateChannel::kBeta;
  }
  if (value == "nightly") {
    return UpdateChannel::kNightly;
  }
  return std::nullopt;
}

std::string_view UpdateChannelName(UpdateChannel channel) {
  switch (channel) {
    case UpdateChannel::kStable:
      return "stable";
    case UpdateChannel::kBeta:
      return "beta";
    case UpdateChannel::kNightly:
      return "nightly";
  }
  return "stable";
}

std::vector<std::string_view> AllowedSparkleChannels(UpdateChannel channel) {
  switch (channel) {
    case UpdateChannel::kStable:
      return {};
    case UpdateChannel::kBeta:
      return {"beta"};
    case UpdateChannel::kNightly:
      return {"beta", "nightly"};
  }
  return {};
}

}  // namespace ahoi::updater
