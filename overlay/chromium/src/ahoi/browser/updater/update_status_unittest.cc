// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/updater/update_status.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::updater {
namespace {

TEST(UpdateStatusTest, ModelsSuccessfulSparkleJourney) {
  UpdateStatusModel model;
  EXPECT_TRUE(model.SetReady());
  EXPECT_TRUE(model.BeginCheck());
  EXPECT_TRUE(model.FoundUpdate("1.2.3"));
  EXPECT_TRUE(model.BeginDownload());
  EXPECT_TRUE(model.FinishDownload());
  EXPECT_TRUE(model.BeginInstall());
  EXPECT_TRUE(model.BeginRelaunch());
  EXPECT_EQ(UpdateStage::kRelaunching, model.status().stage);
  EXPECT_EQ("1.2.3", model.status().version);
}

TEST(UpdateStatusTest, RefusesImpossibleTransitions) {
  UpdateStatusModel model;
  EXPECT_FALSE(model.BeginDownload());
  EXPECT_TRUE(model.SetReady());
  EXPECT_FALSE(model.BeginInstall());
  EXPECT_EQ(UpdateStage::kIdle, model.status().stage);
}

TEST(UpdateStatusTest, RecoversFromErrorWithNewCheck) {
  UpdateStatusModel model;
  model.SetUnavailable("missing-feed-url");
  EXPECT_TRUE(model.SetReady());
  model.Fail("network unavailable");
  EXPECT_EQ(UpdateStage::kError, model.status().stage);
  EXPECT_TRUE(model.BeginCheck());
  EXPECT_TRUE(model.status().error.empty());
}

}  // namespace
}  // namespace ahoi::updater
