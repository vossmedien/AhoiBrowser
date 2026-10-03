// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <memory>
#include <string>

#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

class DeveloperNetworkWorkerBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &DeveloperNetworkWorkerBrowserTest::Handle, base::Unretained(this),
        &own_requests_));
    foreign_.RegisterRequestHandler(base::BindRepeating(
        &DeveloperNetworkWorkerBrowserTest::Handle, base::Unretained(this),
        &foreign_requests_));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(foreign_.Start());
  }
  void TearDownOnMainThread() override {
    EXPECT_TRUE(foreign_.ShutdownAndWaitUntilComplete());
    EXPECT_TRUE(embedded_test_server()->ShutdownAndWaitUntilComplete());
    InProcessBrowserTest::TearDownOnMainThread();
  }
  std::unique_ptr<net::test_server::HttpResponse> Handle(
      std::atomic<int>* count,
      const net::test_server::HttpRequest& request) {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_code(net::HTTP_OK);
    if (request.relative_url == "/owner") {
      response->set_content_type("text/html");
      response->set_content("<title>native worker fixture</title>");
    } else if (request.relative_url == "/worker.js") {
      response->set_content_type("application/javascript");
      response->set_content(R"JS(
        onmessage = async event => {
          try {
            const first = await fetch(event.data);
            const header = first.headers.get('X-Ahoi-Resp');
            const a = await first.text();
            const second = await fetch(event.data);
            postMessage(a + ';' + await second.text() + ';' + header);
          } catch (error) { postMessage('error'); }
        };
      )JS");
    } else if (request.relative_url == "/worker-cache") {
      response->set_content_type("text/plain");
      response->AddCustomHeader("Cache-Control", "max-age=3600");
      response->AddCustomHeader("Access-Control-Allow-Origin", "*");
      auto header = request.headers.find("X-Ahoi-Dev");
      const std::string marker = header == request.headers.end()
                                     ? std::string() : header->second;
      response->set_content(std::to_string(++*count) + "|" + marker);
    } else {
      return nullptr;
    }
    return response;
  }
  void Configure(bool enabled, bool cache_disabled = true) {
    auto* prefs = browser()->GetProfile()->GetPrefs();
    prefs->SetBoolean(developer_toolkit_prefs::kToolkitEnabled, enabled);
    DeveloperProfile profile{.name = "Native worker network fixture"};
    profile.cache_disabled = cache_disabled;
    profile.header_rules_enabled = true;
    profile.header_rules.push_back({.name = "X-Ahoi-Dev", .value = "configured"});
    profile.response_header_rules_enabled = true;
    profile.response_header_rules.push_back({.name = "X-Ahoi-Resp", .value = "present"});
    PrefDeveloperProfileStore store(prefs, false);
    ASSERT_TRUE(store.Set(url::Origin::Create(embedded_test_server()->GetURL("/")),
                          profile));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/owner")));
  }
  std::string RunWorker(const GURL& target) {
    auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
    return content::EvalJs(contents, content::JsReplace(R"JS(
      new Promise((resolve, reject) => {
        const worker = new Worker('/worker.js');
        worker.onmessage = event => { worker.terminate(); resolve(event.data); };
        worker.onerror = event => { worker.terminate(); reject('worker failed'); };
        worker.postMessage($1);
      });
    )JS", target.spec())).ExtractString();
  }
  std::string RunExistingWorker() {
    auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
    return content::EvalJs(contents, R"JS(
      new Promise((resolve, reject) => {
        window.cacheWorker.onmessage = event => resolve(event.data);
        window.cacheWorker.onerror = () => reject('worker failed');
        window.cacheWorker.postMessage('/worker-cache');
      });
    )JS").ExtractString();
  }

  std::string RunDocumentFetch() {
    auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
    return content::EvalJs(contents, R"JS(
      (async () => {
        const first = await fetch('/worker-cache');
        const header = first.headers.get('X-Ahoi-Resp');
        const a = await first.text();
        const second = await fetch('/worker-cache');
        return a + ';' + await second.text() + ';' + header;
      })();
    )JS").ExtractString();
  }

  std::atomic<int> own_requests_ = 0;
  std::atomic<int> foreign_requests_ = 0;
  net::EmbeddedTestServer foreign_;
};

IN_PROC_BROWSER_TEST_F(DeveloperNetworkWorkerBrowserTest,
                       DocumentFactoryUsesTheNavigationBeingCommitted) {
  Configure(true);
  EXPECT_EQ("1|configured;2|configured;present", RunDocumentFetch());
  EXPECT_EQ(2, own_requests_);
  // A later same-origin document must receive its own factory ownership,
  // rather than reuse the previous document's navigation identity.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/owner")));
  EXPECT_EQ("3|configured;4|configured;present", RunDocumentFetch());
  EXPECT_EQ(4, own_requests_);
}

IN_PROC_BROWSER_TEST_F(DeveloperNetworkWorkerBrowserTest,
                       DisabledToolkitLeavesNativeDocumentDefaults) {
  Configure(false);
  EXPECT_EQ("1|;1|;null", RunDocumentFetch());
  EXPECT_EQ(1, own_requests_);
}

IN_PROC_BROWSER_TEST_F(DeveloperNetworkWorkerBrowserTest,
                       TabCacheRefreshSurvivesCancelledReloadAndKeepsWorker) {
  Configure(true, false);
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  auto* helper = DeveloperProfileTabHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  auto* prefs = browser()->GetProfile()->GetPrefs();
  const auto saved = prefs->GetDict(kDeveloperProfilesPref).Clone();
  const auto navigation = contents->GetPrimaryMainFrame()->GetNavigationId();
  ASSERT_TRUE(content::ExecJs(contents, R"JS(
    window.cacheWorker = new Worker('/worker.js');
    window.fixtureDraft = 'keep this synthetic draft';
    window.onbeforeunload = event => {
      event.preventDefault();
      event.returnValue = '';
    };
  )JS"));
  base::ScopedClosureRunner fixture_cleanup(base::BindOnce(
      [](base::WeakPtr<content::WebContents> live) {
        if (live && !live->IsBeingDestroyed()) {
          EXPECT_TRUE(content::ExecJs(live.get(), R"JS(
            window.onbeforeunload = null;
            if (window.cacheWorker) window.cacheWorker.terminate();
          )JS"));
        }
      }, contents->GetWeakPtr()));
  EXPECT_EQ("1|configured;1|configured;present", RunExistingWorker());
  EXPECT_EQ(1, own_requests_);
  content::PrepContentsForBeforeUnloadTest(contents);
  ASSERT_TRUE(helper->SetCacheDisabledForCurrentTab(true));
  contents->GetController().Reload(content::ReloadType::NORMAL, true);
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog);
  dialog->view()->CancelAppModalDialog();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(navigation, contents->GetPrimaryMainFrame()->GetNavigationId());
  EXPECT_EQ("keep this synthetic draft",
            content::EvalJs(contents, "window.fixtureDraft"));
  EXPECT_TRUE(helper->IsCacheDisabledForCurrentTab());
  EXPECT_EQ(saved, prefs->GetDict(kDeveloperProfilesPref));
  // Same already-created worker and same document: neither is recreated by
  // the cancelled reload. Both existing header directions must still work.
  EXPECT_EQ("2|configured;3|configured;present", RunExistingWorker());
  EXPECT_EQ(3, own_requests_);
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload = null"));
  ASSERT_TRUE(helper->SetCacheDisabledForCurrentTab(false));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ("1|configured;1|configured;present", RunExistingWorker());
  EXPECT_EQ(3, own_requests_);
  EXPECT_EQ(saved, prefs->GetDict(kDeveloperProfilesPref));
  EXPECT_EQ(navigation, contents->GetPrimaryMainFrame()->GetNavigationId());
  ASSERT_TRUE(content::ExecJs(contents, "window.cacheWorker.terminate()"));
}

IN_PROC_BROWSER_TEST_F(DeveloperNetworkWorkerBrowserTest,
                       DedicatedWorkerUsesParentRulesAndCacheOptIn) {
  Configure(true);
  EXPECT_EQ("1|configured;2|configured;present",
            RunWorker(embedded_test_server()->GetURL("/worker-cache")));
  EXPECT_EQ(2, own_requests_);
}

IN_PROC_BROWSER_TEST_F(DeveloperNetworkWorkerBrowserTest,
                       DisabledToolkitLeavesNativeWorkerDefaults) {
  Configure(false);
  EXPECT_EQ("1|;1|;null",
            RunWorker(embedded_test_server()->GetURL("/worker-cache")));
  EXPECT_EQ(1, own_requests_);
}

IN_PROC_BROWSER_TEST_F(DeveloperNetworkWorkerBrowserTest,
                       CrossOriginWorkerFetchBypassesCacheWithoutHeaderLeak) {
  Configure(true);
  EXPECT_EQ("1|;2|;null", RunWorker(foreign_.GetURL("/worker-cache")));
  EXPECT_EQ(2, foreign_requests_);
}

}  // namespace
}  // namespace ahoi
