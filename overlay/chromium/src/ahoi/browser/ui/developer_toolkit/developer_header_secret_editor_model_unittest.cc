// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_header_secret_editor_model.h"

#include <string>
#include <utility>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

constexpr char kOriginalReference[] = "ahoi-keychain:original-test-item";
constexpr char kRotatedReference[] = "ahoi-keychain:rotated-test-item";

DeveloperHeaderRule PlainRule(std::string name, std::string value) {
  return {.name = std::move(name), .value = std::move(value)};
}

DeveloperHeaderRule SecretRule(std::string name, std::string reference) {
  return {.name = std::move(name), .secret_reference = std::move(reference)};
}

TEST(DeveloperHeaderSecretEditorModelTest,
     DisplayAndPlainEditorStateCannotExposeOpaqueReferences) {
  DeveloperHeaderSecretEditorModel model(
      false,
      {PlainRule("X-Debug", "enabled"),
       SecretRule("Authorization", kOriginalReference)},
      {});
  ASSERT_TRUE(model.valid());

  const auto plain =
      model.PlainRulesForEditor(DeveloperHeaderSecretDirection::kRequest);
  ASSERT_EQ(1u, plain.size());
  EXPECT_EQ("X-Debug", plain[0].name);
  EXPECT_TRUE(plain[0].secret_reference.empty());

  const auto display = model.DisplayEntries();
  ASSERT_EQ(1u, display.size());
  EXPECT_EQ("Authorization", display[0].header_name);
  EXPECT_EQ(DeveloperHeaderSecretDirection::kRequest, display[0].direction);

  DeveloperProfile profile;
  profile.header_rules = plain;
  ASSERT_TRUE(model.ApplyToProfile(&profile));
  ASSERT_EQ(2u, profile.header_rules.size());
  EXPECT_EQ(kOriginalReference, profile.header_rules[1].secret_reference);
}

TEST(DeveloperHeaderSecretEditorModelTest,
     RotationKeepsOldItemUntilSuccessfulProfileCommit) {
  DeveloperHeaderSecretEditorModel model(
      false, {SecretRule("Authorization", kOriginalReference)}, {});
  ASSERT_TRUE(model.AddOrRotate(DeveloperHeaderSecretDirection::kRequest,
                                "authorization", kRotatedReference));

  DeveloperProfile profile;
  ASSERT_TRUE(model.ApplyToProfile(&profile));
  ASSERT_EQ(1u, profile.header_rules.size());
  EXPECT_EQ(kRotatedReference, profile.header_rules[0].secret_reference);

  const auto removed = model.TakeReferencesToRemoveAfterSave();
  ASSERT_EQ(1u, removed.size());
  EXPECT_EQ(kOriginalReference, removed[0]);
  EXPECT_TRUE(model.TakeReferencesToRemoveOnCancel().empty());
}

TEST(DeveloperHeaderSecretEditorModelTest, CancelOwnsOnlyNewUncommittedItems) {
  DeveloperHeaderSecretEditorModel model(
      false, {SecretRule("Authorization", kOriginalReference)}, {});
  ASSERT_TRUE(model.AddOrRotate(DeveloperHeaderSecretDirection::kRequest,
                                "X-Api-Key", kRotatedReference));
  const auto removed = model.TakeReferencesToRemoveOnCancel();
  ASSERT_EQ(1u, removed.size());
  EXPECT_EQ(kRotatedReference, removed[0]);
}

TEST(DeveloperHeaderSecretEditorModelTest,
     DeleteRequiresTwoMatchingConfirmations) {
  DeveloperHeaderSecretEditorModel model(
      false, {SecretRule("Authorization", kOriginalReference)}, {});
  EXPECT_EQ(DeveloperHeaderSecretDeleteResult::kConfirmationArmed,
            model.RequestDelete(DeveloperHeaderSecretDirection::kRequest,
                                "Authorization"));
  ASSERT_EQ(1u, model.DisplayEntries().size());
  EXPECT_TRUE(model.DisplayEntries()[0].delete_confirmation_armed);
  EXPECT_EQ(DeveloperHeaderSecretDeleteResult::kDeleted,
            model.RequestDelete(DeveloperHeaderSecretDirection::kRequest,
                                "authorization"));
  EXPECT_TRUE(model.DisplayEntries().empty());

  DeveloperProfile profile;
  ASSERT_TRUE(model.ApplyToProfile(&profile));
  const auto removed = model.TakeReferencesToRemoveAfterSave();
  ASSERT_EQ(1u, removed.size());
  EXPECT_EQ(kOriginalReference, removed[0]);
}

TEST(DeveloperHeaderSecretEditorModelTest,
     OffTheRecordAndManualReferenceInputFailClosed) {
  DeveloperHeaderSecretEditorModel off_the_record(
      true, {SecretRule("Authorization", kOriginalReference)}, {});
  EXPECT_FALSE(off_the_record.valid());
  EXPECT_TRUE(off_the_record.DisplayEntries().empty());
  EXPECT_FALSE(
      off_the_record.AddOrRotate(DeveloperHeaderSecretDirection::kRequest,
                                 "Authorization", kRotatedReference));

  DeveloperHeaderSecretEditorModel regular(false, {}, {});
  DeveloperProfile profile;
  profile.header_rules.push_back(
      SecretRule("Authorization", kOriginalReference));
  EXPECT_FALSE(regular.ApplyToProfile(&profile));
}

TEST(DeveloperHeaderSecretEditorModelTest,
     DuplicatePlainAndSecretHeaderNamesFailAtomically) {
  DeveloperHeaderSecretEditorModel model(
      false, {SecretRule("Authorization", kOriginalReference)}, {});
  DeveloperProfile profile;
  profile.header_rules.push_back(PlainRule("authorization", "visible"));
  const DeveloperProfile before = profile;
  EXPECT_FALSE(model.ApplyToProfile(&profile));
  EXPECT_EQ(before, profile);
}

}  // namespace
}  // namespace ahoi
