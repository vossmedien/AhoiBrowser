// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_header_secret_editor_view.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view_utils.h"

namespace ahoi {
namespace {

constexpr char kReference[] = "ahoi-keychain:ui-model-test-item";

struct FakeSecretStoreState {
  std::atomic<int> store_count = 0;
  std::atomic<int> remove_count = 0;
};

class FakeSecretStore final : public DeveloperSecretStore {
 public:
  explicit FakeSecretStore(std::shared_ptr<FakeSecretStoreState> state)
      : state_(std::move(state)) {}

  std::optional<std::string> Store(std::string_view label,
                                   std::string_view secret) override {
    if (label.empty() || secret.empty()) {
      return std::nullopt;
    }
    ++state_->store_count;
    return kReference;
  }

  std::optional<std::string> Resolve(
      std::string_view reference) const override {
    return std::nullopt;
  }

  bool Remove(std::string_view reference) override {
    if (reference != kReference) {
      return false;
    }
    ++state_->remove_count;
    return true;
  }

 private:
  const std::shared_ptr<FakeSecretStoreState> state_;
};

DeveloperSecretStoreFactory FakeFactory(
    std::shared_ptr<FakeSecretStoreState> state) {
  return base::BindRepeating(
      [](std::shared_ptr<FakeSecretStoreState> state)
          -> std::unique_ptr<DeveloperSecretStore> {
        return std::make_unique<FakeSecretStore>(std::move(state));
      },
      std::move(state));
}

bool LabelsContain(const views::View* root, std::u16string_view needle) {
  if (const auto* label = views::AsViewClass<views::Label>(root);
      label && label->GetText().find(needle) != std::u16string::npos) {
    return true;
  }
  for (const views::View* child : root->children()) {
    if (LabelsContain(child, needle)) {
      return true;
    }
  }
  return false;
}

class DeveloperHeaderSecretEditorViewTest : public views::ViewsTestBase {};

DeveloperHeaderSecretEditorView::StatusCallback NoStatusCallback() {
  return base::BindRepeating([](std::u16string, bool) {});
}

TEST_F(DeveloperHeaderSecretEditorViewTest,
       ExistingReferenceIsRepresentedOnlyByMaskedProductState) {
  auto state = std::make_shared<FakeSecretStoreState>();
  DeveloperHeaderRule secret_rule{
      .name = "Authorization",
      .secret_reference = kReference,
  };
  auto view = std::make_unique<DeveloperHeaderSecretEditorView>(
      false, std::vector<DeveloperHeaderRule>{secret_rule},
      std::vector<DeveloperHeaderRule>(), FakeFactory(std::move(state)),
      NoStatusCallback());

  EXPECT_EQ(ui::TEXT_INPUT_TYPE_PASSWORD,
            view->secret_field_for_testing()->GetTextInputType());
  EXPECT_EQ(1u, view->model_for_testing().DisplayEntries().size());
  EXPECT_FALSE(LabelsContain(
      view.get(), base::UTF8ToUTF16(std::string_view(kReference))));
  EXPECT_TRUE(LabelsContain(view.get(), u"••••••••"));
}

TEST_F(DeveloperHeaderSecretEditorViewTest,
       PrimaryNavigationClearsPlaintextAndFailsClosed) {
  auto view = std::make_unique<DeveloperHeaderSecretEditorView>(
      false, std::vector<DeveloperHeaderRule>(),
      std::vector<DeveloperHeaderRule>(), DeveloperSecretStoreFactory(),
      NoStatusCallback());
  view->secret_field_for_testing()->SetText(u"transient-token");

  view->OnPrimaryNavigationStarted();

  EXPECT_TRUE(view->secret_field_for_testing()->GetText().empty());
  EXPECT_FALSE(view->valid());
  EXPECT_FALSE(view->store_button_for_testing()->GetEnabled());
}

TEST_F(DeveloperHeaderSecretEditorViewTest, OffTheRecordStateIsUnavailable) {
  auto view = std::make_unique<DeveloperHeaderSecretEditorView>(
      true, std::vector<DeveloperHeaderRule>(),
      std::vector<DeveloperHeaderRule>(), DeveloperSecretStoreFactory(),
      NoStatusCallback());
  EXPECT_FALSE(view->valid());
  EXPECT_FALSE(view->secret_field_for_testing()->GetEnabled());
  EXPECT_FALSE(view->store_button_for_testing()->GetEnabled());
}

TEST_F(DeveloperHeaderSecretEditorViewTest,
       CreateWipesInputAndCloseRemovesUncommittedItem) {
  auto state = std::make_shared<FakeSecretStoreState>();
  auto view = std::make_unique<DeveloperHeaderSecretEditorView>(
      false, std::vector<DeveloperHeaderRule>(),
      std::vector<DeveloperHeaderRule>(), FakeFactory(state),
      NoStatusCallback());
  view->header_name_field_for_testing()->SetText(u"Authorization");
  view->secret_field_for_testing()->SetText(u"write-only-token");

  views::test::ButtonTestApi(view->store_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_TRUE(view->secret_field_for_testing()->GetText().empty());
  task_environment()->RunUntilIdle();

  EXPECT_EQ(1, state->store_count.load());
  ASSERT_EQ(1u, view->model_for_testing().DisplayEntries().size());
  EXPECT_FALSE(LabelsContain(
      view.get(), base::UTF8ToUTF16(std::string_view(kReference))));

  view.reset();
  task_environment()->RunUntilIdle();
  EXPECT_EQ(1, state->remove_count.load());
}

TEST_F(DeveloperHeaderSecretEditorViewTest,
       SuccessfulProfileCommitAdoptsCreatedItem) {
  auto state = std::make_shared<FakeSecretStoreState>();
  auto view = std::make_unique<DeveloperHeaderSecretEditorView>(
      false, std::vector<DeveloperHeaderRule>(),
      std::vector<DeveloperHeaderRule>(), FakeFactory(state),
      NoStatusCallback());
  view->header_name_field_for_testing()->SetText(u"Authorization");
  view->secret_field_for_testing()->SetText(u"write-only-token");
  views::test::ButtonTestApi(view->store_button_for_testing())
      .NotifyDefaultMouseClick();
  task_environment()->RunUntilIdle();

  DeveloperProfile profile;
  ASSERT_TRUE(view->ApplyToProfile(&profile));
  ASSERT_TRUE(view->BeginProfileCommit());
  view->CompleteProfileCommit(true);
  view.reset();
  task_environment()->RunUntilIdle();

  EXPECT_EQ(1, state->store_count.load());
  EXPECT_EQ(0, state->remove_count.load());
}

}  // namespace
}  // namespace ahoi
