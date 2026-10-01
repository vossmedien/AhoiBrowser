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

}  // namespace
}  // namespace ahoi
