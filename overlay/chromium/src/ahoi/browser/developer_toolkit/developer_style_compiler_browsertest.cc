// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_style_compiler.h"

#include "ahoi/browser/developer_toolkit/developer_style_compiler_service_client.h"
#include "ahoi/browser/developer_toolkit/public/mojom/developer_style_compiler.mojom.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/test_future.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/browser/service_process_info.h"
#include "content/public/test/browser_test.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

class DeveloperStyleCompilerProcessObserver final
    : public content::ServiceProcessHost::Observer {
 public:
  DeveloperStyleCompilerProcessObserver() {
    content::ServiceProcessHost::AddObserver(this);
    for (const content::ServiceProcessInfo& info :
         content::ServiceProcessHost::GetRunningProcessInfo()) {
      if (info.IsService<developer_toolkit::mojom::DeveloperStyleCompiler>()) {
        running_ = true;
      }
    }
  }
  DeveloperStyleCompilerProcessObserver(
      const DeveloperStyleCompilerProcessObserver&) = delete;
  DeveloperStyleCompilerProcessObserver& operator=(
      const DeveloperStyleCompilerProcessObserver&) = delete;
  ~DeveloperStyleCompilerProcessObserver() override {
    content::ServiceProcessHost::RemoveObserver(this);
  }

  bool running() const { return running_; }
  void WaitForLaunch() {
    if (!running_) {
      launch_loop_.Run();
    }
  }
  void WaitForTermination() {
    if (running_) {
      termination_loop_.Run();
    }
  }

 private:
  void OnServiceProcessLaunched(
      const content::ServiceProcessInfo& info) override {
    if (!info.IsService<developer_toolkit::mojom::DeveloperStyleCompiler>()) {
      return;
    }
    EXPECT_FALSE(running_);
    running_ = true;
    launch_loop_.Quit();
  }

  void OnServiceProcessTerminatedNormally(
      const content::ServiceProcessInfo& info) override {
    if (!info.IsService<developer_toolkit::mojom::DeveloperStyleCompiler>()) {
      return;
    }
    EXPECT_TRUE(running_);
    running_ = false;
    termination_loop_.Quit();
  }

  void OnServiceProcessCrashed(
      const content::ServiceProcessInfo& info) override {
    if (!info.IsService<developer_toolkit::mojom::DeveloperStyleCompiler>()) {
      return;
    }
    ADD_FAILURE() << "Developer style compiler Utility process crashed";
    running_ = false;
    launch_loop_.Quit();
    termination_loop_.Quit();
  }

  bool running_ = false;
  base::RunLoop launch_loop_;
  base::RunLoop termination_loop_;
};

using DeveloperStyleCompilerBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DeveloperStyleCompilerBrowserTest,
                       LaunchesLazilyAndTerminatesWithEditor) {
  EXPECT_EQ(content::GetServiceSandboxType<
                developer_toolkit::mojom::DeveloperStyleCompiler>(),
            sandbox::mojom::Sandbox::kService);

  DeveloperStyleCompilerProcessObserver observer;
  LazyDeveloperStyleCompiler compiler(
      base::BindRepeating(&CreateSandboxedDeveloperStyleCompilerService));
  EXPECT_FALSE(observer.running());
  EXPECT_FALSE(compiler.service_loaded_for_testing());

  compiler.OpenEditor();
  EXPECT_FALSE(observer.running());
  EXPECT_FALSE(compiler.service_loaded_for_testing());

  base::test::TestFuture<DeveloperStyleCompileResult> future;
  compiler.Compile({.language = DeveloperStyleLanguage::kLess,
                    .source = "@brand: #0a84ff; .card { color: @brand; }"},
                   future.GetCallback());
  observer.WaitForLaunch();
  DeveloperStyleCompileResult result = future.Take();
  ASSERT_TRUE(result.succeeded());
  EXPECT_EQ(result.css, ".card{color: #0a84ff;}\n");
  EXPECT_TRUE(observer.running());
  EXPECT_TRUE(compiler.service_loaded_for_testing());

  base::test::TestFuture<DeveloperStyleCompileResult> sass_future;
  compiler.Compile({.language = DeveloperStyleLanguage::kSass,
                    .source = "$brand: #ff375f\n.card\n  color: $brand\n"},
                   sass_future.GetCallback());
  DeveloperStyleCompileResult sass_result = sass_future.Take();
  ASSERT_TRUE(sass_result.succeeded());
  EXPECT_EQ(sass_result.css, ".card{color: #ff375f;}\n");
  EXPECT_TRUE(observer.running());

  base::test::TestFuture<DeveloperStyleCompileResult> import_future;
  compiler.Compile({.language = DeveloperStyleLanguage::kLess,
                    .source = "@import 'network-or-file.less';"},
                   import_future.GetCallback());
  DeveloperStyleCompileResult import_result = import_future.Take();
  EXPECT_EQ(import_result.status,
            DeveloperStyleCompileStatus::kUnsupportedSyntax);
  EXPECT_TRUE(import_result.css.empty());

  compiler.CloseEditor();
  EXPECT_FALSE(compiler.service_loaded_for_testing());
  observer.WaitForTermination();
  EXPECT_FALSE(observer.running());
}

}  // namespace
}  // namespace ahoi
