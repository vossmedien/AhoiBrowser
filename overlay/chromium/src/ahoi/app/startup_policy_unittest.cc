// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/app/startup_policy.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::startup {
namespace {

std::vector<std::string> FeaturesForSwitch(
    const base::CommandLine& command_line,
    std::string_view switch_name) {
  std::vector<std::string> result;
  for (std::string_view feature : base::FeatureList::SplitFeatureListString(
           command_line.GetSwitchValueASCII(switch_name))) {
    result.emplace_back(feature);
  }
  return result;
}

TEST(StartupPolicyTest, DisablesBackgroundAccountAndAiFeatures) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);

  ApplyEarlyStartupPolicy(command_line);

  const std::vector<std::string> disabled =
      FeaturesForSwitch(command_line, switches::kDisableFeatures);
  EXPECT_NE(disabled.end(), std::ranges::find(disabled, "AimEnabled"));
  EXPECT_NE(disabled.end(), std::ranges::find(disabled, "ContextualTasks"));
  EXPECT_NE(disabled.end(),
            std::ranges::find(disabled, "ContextualTasksSidePanel"));
  EXPECT_NE(disabled.end(),
            std::ranges::find(disabled, "ContextualTasksContext"));
  EXPECT_NE(disabled.end(), std::ranges::find(disabled, "OptimizationHints"));
  EXPECT_NE(disabled.end(),
            std::ranges::find(disabled, "OptimizationGuideOnDeviceModel"));
  EXPECT_NE(disabled.end(),
            std::ranges::find(disabled, "OptimizationGuideManifestBroker"));
  EXPECT_NE(disabled.end(), std::ranges::find(disabled, "AIPromptAPI"));
}

TEST(StartupPolicyTest, OverridesHostileEnablesAndPreservesOtherArguments) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(switches::kEnableFeatures,
                                 "UnrelatedFeature,AimEnabled<AimTrial,"
                                 "OptimizationGuideModelExecution:param/value");
  command_line.AppendSwitchASCII(
      switches::kDisableFeatures,
      "UnrelatedDisabled,OptimizationHints<StaleTrial");
  command_line.AppendArg("https://accounts.google.com/");
  const base::CommandLine::StringVector original_args = command_line.GetArgs();

  ApplyEarlyStartupPolicy(command_line);
  ApplyEarlyStartupPolicy(command_line);

  EXPECT_EQ("UnrelatedFeature",
            command_line.GetSwitchValueASCII(switches::kEnableFeatures));
  EXPECT_EQ(original_args, command_line.GetArgs());

  const std::vector<std::string> disabled =
      FeaturesForSwitch(command_line, switches::kDisableFeatures);
  EXPECT_EQ(1, std::ranges::count(disabled, "OptimizationHints"));
  EXPECT_EQ(1, std::ranges::count(disabled, "AimEnabled"));
  EXPECT_EQ(1, std::ranges::count(disabled, "UnrelatedDisabled"));
}

}  // namespace
}  // namespace ahoi::startup
