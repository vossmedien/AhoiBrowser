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
#include "components/prefs/testing_pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_content_client_initializer.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi {
namespace {

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

class DeveloperProfileEditorActivationTest : public views::ViewsTestBase {
 protected:
  DeveloperProfileEditorActivationTest()
      : views::ViewsTestBase(std::unique_ptr<base::test::TaskEnvironment>(
            std::make_unique<content::BrowserTaskEnvironment>(
                content::BrowserTaskEnvironment::MainThreadType::UI))) {}

  void SetUp() override {
    views::ViewsTestBase::SetUp();
    content_clients_ = std::make_unique<content::TestContentClientInitializer>();
    content_clients_->CreateTestRenderViewHosts();
    context_ = std::make_unique<content::TestBrowserContext>();
    // Toolkit registration also owns the profile dictionary.
    developer_toolkit_prefs::RegisterProfilePrefs(prefs_.registry());
    user_prefs::UserPrefs::Set(context_.get(), &prefs_);
    contents_ = content::WebContentsTester::CreateTestWebContents(
        context_.get(), nullptr);
    content::NavigationSimulator::NavigateAndCommitFromBrowser(
        contents_.get(), GURL("https://editor.test/"));
  }
  void TearDown() override {
    editor_.reset();
    contents_.reset();
    context_.reset();
    content_clients_.reset();
    views::ViewsTestBase::TearDown();
  }
  void OpenEditor() {
    DeveloperProfile profile{.name = "Protected draft"};
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
        base::BindRepeating([] { return false; }), base::DoNothing(), &prefs_,
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

  TestingPrefServiceSimple prefs_;
  std::unique_ptr<content::TestContentClientInitializer> content_clients_;
  std::unique_ptr<content::TestBrowserContext> context_;
  std::unique_ptr<content::WebContents> contents_;
  std::shared_ptr<CompilerFixture> compiler_ =
      std::make_shared<CompilerFixture>();
  std::optional<DeveloperProfile> saved_;
  std::unique_ptr<DeveloperProfileEditorView> editor_;
};

TEST_F(DeveloperProfileEditorActivationTest,
       DisabledEditorStartsNoCompilerAndPreservesDraft) {
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, false);
  OpenEditor();
  EXPECT_FALSE(editor_->Save());
  EXPECT_EQ(0, compiler_->created);
  EXPECT_FALSE(saved_);
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
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
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
  OpenEditor();
  EXPECT_FALSE(editor_->Save());
  ASSERT_EQ(1, compiler_->created);
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, false);
  EXPECT_EQ(1, compiler_->destroyed);
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
  EXPECT_EQ(1, compiler_->created);
  Reply(0);
  EXPECT_FALSE(saved_);
  EXPECT_FALSE(editor_->Save());
  ASSERT_EQ(2, compiler_->created);
  Reply(1);
  ASSERT_TRUE(saved_);
  EXPECT_EQ("Protected draft", saved_->name);
}

}  // namespace
}  // namespace ahoi
