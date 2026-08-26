// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/app/startup_policy.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/strings/string_util.h"

namespace ahoi::startup {
namespace {

// AhoiBrowser v1 intentionally has no AI platform. Keep the product policy at
// the earliest startup boundary so Finch, field trials, or hostile command-line
// enables cannot silently reactivate background account/AI traffic.
constexpr auto kProductDisabledFeatures = std::to_array<std::string_view>({
    "AimEnabled",
    "AimServerEligibilityEnabled",
    "AimServerRequestOnStartupEnabled",
    "AimServerRequestOnIdentityChangeEnabled",
    "ContextualTasks",
    "ContextualTasksSidePanel",
    "ContextualTasksContext",
    "OptimizationHints",
    "OptimizationHintsFetchingSRP",
    "OptimizationGuideModelExecution",
    "OptimizationGuideOnDeviceModel",
    "OptimizationGuideManifestBroker",
    "ModelQualityLogging",
    "BuiltInAIEagerInit",
    "HistoryEmbeddings",
    "HistoryEmbeddingsAnswers",
    "AIPromptAPI",
    "AIProofreadingAPI",
    "AIRewriterAPI",
    "AISummarizationAPI",
    "AIWriterAPI",
});

std::string_view FeatureName(std::string_view entry) {
  if (entry.starts_with('*')) {
    entry.remove_prefix(1);
  }
  const size_t separator = entry.find_first_of("<:");
  return entry.substr(0, separator);
}

bool IsProductDisabledFeature(std::string_view entry) {
  const std::string_view name = FeatureName(entry);
  for (std::string_view disabled : kProductDisabledFeatures) {
    if (name == disabled) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> ReadFeatureSwitch(
    const base::CommandLine& command_line,
    std::string_view switch_name) {
  std::vector<std::string> result;
  for (std::string_view feature : base::FeatureList::SplitFeatureListString(
           command_line.GetSwitchValueASCII(switch_name))) {
    result.emplace_back(feature);
  }
  return result;
}

void WriteFeatureSwitch(base::CommandLine& command_line,
                        std::string_view switch_name,
                        const std::vector<std::string>& features) {
  command_line.RemoveSwitch(switch_name);
  if (!features.empty()) {
    command_line.AppendSwitchASCII(switch_name,
                                   base::JoinString(features, ","));
  }
}

}  // namespace

void ApplyEarlyStartupPolicy(base::CommandLine& command_line) {
  std::vector<std::string> enabled =
      ReadFeatureSwitch(command_line, switches::kEnableFeatures);
  std::erase_if(enabled, IsProductDisabledFeature);
  WriteFeatureSwitch(command_line, switches::kEnableFeatures, enabled);

  std::vector<std::string> disabled =
      ReadFeatureSwitch(command_line, switches::kDisableFeatures);
  std::erase_if(disabled, IsProductDisabledFeature);
  for (std::string_view feature : kProductDisabledFeatures) {
    disabled.emplace_back(feature);
  }
  WriteFeatureSwitch(command_line, switches::kDisableFeatures, disabled);
}

}  // namespace ahoi::startup
