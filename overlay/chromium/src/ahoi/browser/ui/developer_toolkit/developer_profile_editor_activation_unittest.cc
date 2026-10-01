// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_profile_editor_view.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "components/prefs/pref_service.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/navigation_simulator.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/view_utils.h"
#include "ui/views/test/combobox_test_api.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi {
namespace {

views::Combobox* FindHeaderLifetime(views::View* view) {
  if (auto* combo = views::AsViewClass<views::Combobox>(view)) {
    if (combo->GetModel()->GetItemCount() == 2 &&
        combo->GetModel()->GetItemAt(0) == l10n_util::GetStringUTF16(
            IDS_AHOI_DEVELOPER_HEADER_LIFETIME_TAB)) {
      return combo;
    }
  }
  for (const auto& child : view->children()) {
    if (auto* combo = FindHeaderLifetime(child.get())) {
      return combo;
    }
  }
  return nullptr;
}

void FindHeaderSyncControls(views::View* view,
                            std::vector<views::Checkbox*>* controls) {
  if (auto* checkbox = views::AsViewClass<views::Checkbox>(view)) {
    if (checkbox->GetText() == l10n_util::GetStringUTF16(
            IDS_AHOI_DEVELOPER_PROFILE_SYNC_HEADERS)) {
      controls->push_back(checkbox);
    }
  }
  for (const auto& child : view->children()) {
    FindHeaderSyncControls(child.get(), controls);
  }
}

struct CompilerFixture {
  int created = 0;
  int destroyed = 0;
  std::vector<DeveloperStyleCompileCallback> replies;
};

class FixtureCompiler final : public DeveloperStyleCompilerService {
 public:
  explicit FixtureCompiler(std::shared_ptr<CompilerFixture> state)
      : state_(std::move(state)) { ++state_->created; }
  ~FixtureCompiler() override { ++state_->destroyed; }
  void Compile(DeveloperStyleCompileRequest,
               DeveloperStyleCompileCallback reply) override {
    state_->replies.push_back(std::move(reply));
  }

 private:
  const std::shared_ptr<CompilerFixture> state_;
};

class DeveloperProfileEditorActivationTest : public ChromeViewsTestBase {
 protected:
  void SetUp() override {
    ChromeViewsTestBase::SetUp();
    context_ = std::make_unique<TestingProfile>();
    prefs_ = context_->GetPrefs();
    contents_ = content::WebContentsTester::CreateTestWebContents(
        context_.get(), nullptr);
    content::NavigationSimulator::NavigateAndCommitFromBrowser(
        contents_.get(), GURL("https://editor.test/"));
  }
  void TearDown() override {
    editor_.reset();
    contents_.reset();
    prefs_ = nullptr;
    context_.reset();
    ChromeViewsTestBase::TearDown();
  }
  void OpenEditor(bool headers_persistent = true, bool sync_headers = false) {
    DeveloperProfile profile{.name = "Protected draft"};
    profile.headers_persistent = headers_persistent;
    profile.header_rules_sync_enabled = sync_headers;
    profile.response_header_rules_sync_enabled = sync_headers;
    profile.assets.push_back({
        .id = "less-draft", .name = "Style",
        .kind = DeveloperAssetKind::kStyle,
        .style_language = DeveloperStyleLanguage::kLess, .enabled = true,
        .source = "@color: red; body { color: @color; }",
        .scope = {.kind = DeveloperAssetScopeKind::kOrigin,
                  .value = "https://editor.test"}});
    editor_ = std::make_unique<DeveloperProfileEditorView>(
        u"https://editor.test", profile, false, contents_.get(),
        base::BindRepeating([]() -> std::unique_ptr<DeveloperSecretStore> {
          return nullptr;
        }),
        base::BindRepeating(
            [](std::optional<DeveloperProfile>* saved,
               const DeveloperProfile& profile) {
              *saved = profile;
              return true;
            }, &saved_),
        base::BindRepeating([] { return false; }), base::DoNothing(), prefs_,
        base::BindRepeating(
            [](std::shared_ptr<CompilerFixture> state)
                -> std::unique_ptr<DeveloperStyleCompilerService> {
              return std::make_unique<FixtureCompiler>(std::move(state));
            }, compiler_));
  }
  void Reply(size_t index) {
    ASSERT_LT(index, compiler_->replies.size());
    std::move(compiler_->replies[index]).Run({
        .status = DeveloperStyleCompileStatus::kSucceeded,
        .css = "body { color: red; }"});
  }

  raw_ptr<PrefService> prefs_ = nullptr;
  content::RenderViewHostTestEnabler renderer_;
  std::unique_ptr<TestingProfile> context_;
  std::unique_ptr<content::WebContents> contents_;
  std::shared_ptr<CompilerFixture> compiler_ =
      std::make_shared<CompilerFixture>();
  std::optional<DeveloperProfile> saved_;
  std::unique_ptr<DeveloperProfileEditorView> editor_;
};

TEST_F(DeveloperProfileEditorActivationTest,
       DisabledEditorStartsNoCompilerAndPreservesDraft) {
  prefs_->SetBoolean(developer_toolkit_prefs::kToolkitEnabled, false);
  OpenEditor();
  EXPECT_FALSE(editor_->Save());
  EXPECT_EQ(0, compiler_->created);
  EXPECT_FALSE(saved_);
  prefs_->SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
  EXPECT_EQ(0, compiler_->created);
  EXPECT_FALSE(editor_->Save());
  ASSERT_EQ(1, compiler_->created);
  Reply(0);
  ASSERT_TRUE(saved_);
  EXPECT_EQ("Protected draft", saved_->name);
  ASSERT_FALSE(saved_->assets.empty());
  EXPECT_EQ("@color: red; body { color: @color; }", saved_->assets[0].source);
}

TEST_F(DeveloperProfileEditorActivationTest,
       DisableDropsCompilerAndRejectsLateReplyAcrossReenable) {
  prefs_->SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
  OpenEditor();
  EXPECT_FALSE(editor_->Save());
  ASSERT_EQ(1, compiler_->created);
  prefs_->SetBoolean(developer_toolkit_prefs::kToolkitEnabled, false);
  EXPECT_EQ(1, compiler_->destroyed);
  prefs_->SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
  EXPECT_EQ(1, compiler_->created);
  Reply(0);
  EXPECT_FALSE(saved_);
  EXPECT_FALSE(editor_->Save());
  ASSERT_EQ(2, compiler_->created);
  Reply(1);
  ASSERT_TRUE(saved_);
  EXPECT_EQ("Protected draft", saved_->name);
}

TEST_F(DeveloperProfileEditorActivationTest,
       TemporaryHeaderLifetimeDisablesBothSyncControlsAndSavesLocalMode) {
  prefs_->SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
  OpenEditor(false, true);
  auto* lifetime = FindHeaderLifetime(editor_.get());
  ASSERT_TRUE(lifetime);
  EXPECT_EQ(0u, lifetime->GetSelectedIndex());
  std::vector<views::Checkbox*> controls;
  FindHeaderSyncControls(editor_.get(), &controls);
  ASSERT_EQ(2u, controls.size());
  for (auto* checkbox : controls) {
    EXPECT_FALSE(checkbox->GetEnabled());
    EXPECT_FALSE(checkbox->GetChecked());
  }
  EXPECT_FALSE(editor_->Save());
  Reply(0);
  ASSERT_TRUE(saved_);
  EXPECT_FALSE(saved_->headers_persistent);
  EXPECT_FALSE(saved_->header_rules_sync_enabled);
  EXPECT_FALSE(saved_->response_header_rules_sync_enabled);
}

TEST_F(DeveloperProfileEditorActivationTest,
       HeaderLifetimeConversionDoesNotRegrantPreviousSyncOptIn) {
  prefs_->SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
  OpenEditor(true, true);
  auto* lifetime = FindHeaderLifetime(editor_.get());
  ASSERT_TRUE(lifetime);
  std::vector<views::Checkbox*> controls;
  FindHeaderSyncControls(editor_.get(), &controls);
  ASSERT_EQ(2u, controls.size());
  views::test::ComboboxTestApi(lifetime).PerformActionAt(0);
  views::test::ComboboxTestApi(lifetime).PerformActionAt(1);
  for (auto* checkbox : controls) {
    EXPECT_TRUE(checkbox->GetEnabled());
    EXPECT_FALSE(checkbox->GetChecked());
  }
  EXPECT_FALSE(editor_->Save());
  Reply(0);
  ASSERT_TRUE(saved_);
  EXPECT_TRUE(saved_->headers_persistent);
  EXPECT_FALSE(saved_->header_rules_sync_enabled);
  EXPECT_FALSE(saved_->response_header_rules_sync_enabled);
}

TEST_F(DeveloperProfileEditorActivationTest,
       HeaderLifetimeChangeRejectsEarlierPersistentCompileCommit) {
  prefs_->SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
  OpenEditor(true, true);
  EXPECT_FALSE(editor_->Save());
  ASSERT_EQ(1, compiler_->created);
  auto* lifetime = FindHeaderLifetime(editor_.get());
  ASSERT_TRUE(lifetime);
  views::test::ComboboxTestApi(lifetime).PerformActionAt(0);
  EXPECT_EQ(1, compiler_->destroyed);
  Reply(0);
  EXPECT_FALSE(saved_);
  EXPECT_FALSE(editor_->Save());
  ASSERT_EQ(2, compiler_->created);
  Reply(1);
  ASSERT_TRUE(saved_);
  EXPECT_FALSE(saved_->headers_persistent);
  EXPECT_FALSE(saved_->header_rules_sync_enabled);
  EXPECT_FALSE(saved_->response_header_rules_sync_enabled);
}

}  // namespace
}  // namespace ahoi
