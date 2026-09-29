// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/popup/link_peek.h"
#include "ahoi/browser/popup/link_peek_input.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/ui/popup/popup_overlay_controller.h"
#include "base/functional/bind.h"
#include "base/synchronization/lock.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ahoi::popup_ui {

// Peek repeats the intercepted request (Crest 02b7f9d6) and takes plain
// target=_blank clicks of saved pages (Crest 351237e3). The embedded server
// records what the previewed site receives.
class AhoiLinkPeekRequestBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_https_test_server().RegisterRequestMonitor(base::BindRepeating(
        &AhoiLinkPeekRequestBrowserTest::OnRequest, base::Unretained(this)));
    ASSERT_TRUE(embedded_https_test_server().Start());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), SourceUrl()));
  }

  PopupOverlayController* controller() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->ahoi_popup_overlay_controller();
  }
  content::WebContents* source() {
    return browser()->GetTabStripModel()->GetActiveWebContents();
  }
  GURL SourceUrl() {
    return embedded_https_test_server().GetURL("a.com", "/title1.html?q=1");
  }
  GURL TargetUrl() {
    return embedded_https_test_server().GetURL("b.com", "/echo?peek");
  }

  // Shows `request` as Peek and returns once the previewed site answered.
  void ShowPeekAndWait(const popup::PeekRequest& request) {
    ASSERT_TRUE(controller()->ShowPeek(source(), request));
    content::WebContents* const peek =
        controller()->popup_contents_for_testing();
    ASSERT_TRUE(peek);
    EXPECT_TRUE(content::WaitForLoadStop(peek));
    EXPECT_EQ(TargetUrl(), peek->GetLastCommittedURL());
  }

  std::optional<std::string> ReceivedHeader(const std::string& name) {
    base::AutoLock lock(lock_);
    EXPECT_TRUE(peek_request_seen_);
    const auto it = peek_headers_.find(name);
    return it == peek_headers_.end() ? std::nullopt
                                     : std::optional<std::string>(it->second);
  }

  void SaveSourceTab() {
    SessionBridge* const bridge =
        SessionBridgeFactory::GetForProfile(browser()->GetProfile());
    ASSERT_TRUE(bridge);
    tabs::TabInterface* const tab =
        browser()->GetTabStripModel()->GetActiveTab();
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return bridge->SaveTabAtWorkspaceRoot(browser(), tab).has_value();
    }));
    ASSERT_TRUE(controller()->IsSavedPage(source()));
  }

  void SetAutoPeek(bool enabled) {
    browser()->GetProfile()->GetPrefs()->SetBoolean(
        popup::kAutoPeekFromSavedPagesPref, enabled);
  }

  // What Chromium hands the browser for a target=_blank link without an
  // opener, before content starts loading it.
  content::WebContents* AddNewForegroundWindow(const GURL& url,
                                               content::WebContents** added) {
    std::unique_ptr<content::WebContents> window =
        content::WebContents::Create(
            content::WebContents::CreateParams(browser()->GetProfile()));
    *added = window.get();
    bool was_blocked = false;
    content::WebContents* const result =
        source()->GetDelegate()->AddNewContents(
            source(), std::move(window), url,
            WindowOpenDisposition::NEW_FOREGROUND_TAB,
            blink::mojom::WindowFeatures(), /*user_gesture=*/true,
            &was_blocked);
    EXPECT_FALSE(was_blocked);
    return result;
  }

 private:
  void OnRequest(const net::test_server::HttpRequest& request) {
    if (request.relative_url != "/echo?peek") {
      return;
    }
    base::AutoLock lock(lock_);
    peek_request_seen_ = true;
    peek_headers_ = request.headers;
  }

  base::Lock lock_;
  bool peek_request_seen_ = false;
  net::test_server::HttpRequest::HeaderMap peek_headers_;
};

IN_PROC_BROWSER_TEST_F(AhoiLinkPeekRequestBrowserTest,
                       NoReferrerPolicyOfThePageIsKept) {
  popup::PeekRequest request(TargetUrl());
  request.referrer =
      content::Referrer(SourceUrl(), network::mojom::ReferrerPolicy::kNever);
  request.initiator_origin = url::Origin::Create(SourceUrl());
  ShowPeekAndWait(request);
  EXPECT_EQ(std::nullopt, ReceivedHeader("Referer"));
  EXPECT_EQ("cross-site", ReceivedHeader("Sec-Fetch-Site"));
}

IN_PROC_BROWSER_TEST_F(AhoiLinkPeekRequestBrowserTest,
                       DefaultPolicySendsOnlyTheOrigin) {
  popup::PeekRequest request(TargetUrl());
  request.referrer = content::Referrer(
      SourceUrl(),
      network::mojom::ReferrerPolicy::kStrictOriginWhenCrossOrigin);
  request.initiator_origin = url::Origin::Create(SourceUrl());
  ShowPeekAndWait(request);
  EXPECT_EQ(SourceUrl().DeprecatedGetOriginAsURL().spec(),
            ReceivedHeader("Referer"));
}

IN_PROC_BROWSER_TEST_F(AhoiLinkPeekRequestBrowserTest,
                       InitiatorIsTheClickedFrameNotTheMainFrame) {
  // A link inside a b.com iframe of the a.com page: the preview's initiator
  // is the iframe's origin, so b.com sees a same-origin request.
  popup::PeekRequest request(TargetUrl());
  request.initiator_origin = url::Origin::Create(TargetUrl());
  ShowPeekAndWait(request);
  EXPECT_EQ("same-origin", ReceivedHeader("Sec-Fetch-Site"));
  EXPECT_EQ(std::nullopt, ReceivedHeader("Referer"));
}

IN_PROC_BROWSER_TEST_F(AhoiLinkPeekRequestBrowserTest,
                       TypedPeekLooksLikeTheOmnibox) {
  ShowPeekAndWait(popup::PeekRequest::ForTypedUrl(TargetUrl(), false));
  EXPECT_EQ(std::nullopt, ReceivedHeader("Referer"));
  EXPECT_EQ("none", ReceivedHeader("Sec-Fetch-Site"));
}

IN_PROC_BROWSER_TEST_F(AhoiLinkPeekRequestBrowserTest,
                       PlainBlankClickOfSavedPageAdoptsTheNewWindow) {
  SaveSourceTab();
  SetAutoPeek(true);
  content::WebContents* added = nullptr;
  content::WebContents* const result =
      AddNewForegroundWindow(TargetUrl(), &added);
  EXPECT_EQ(added, result);
  EXPECT_TRUE(controller()->IsShowing());
  EXPECT_EQ(added, controller()->popup_contents_for_testing());
  EXPECT_EQ(1, browser()->GetTabStripModel()->count());
}

IN_PROC_BROWSER_TEST_F(AhoiLinkPeekRequestBrowserTest,
                       ModifierClickOfSavedPageStaysATab) {
  SaveSourceTab();
  SetAutoPeek(true);
  blink::WebMouseEvent click(
      blink::WebInputEvent::Type::kMouseDown,
      blink::WebInputEvent::kMetaKey | blink::WebInputEvent::kShiftKey,
      blink::WebInputEvent::GetStaticTimeStampForTests());
  click.button = blink::WebPointerProperties::Button::kLeft;
  ASSERT_TRUE(popup::LinkPeekInputTracker::FromWebContents(source()));
  popup::LinkPeekInputTracker::FromWebContents(source())
      ->DidGetUserInteraction(click);
  content::WebContents* added = nullptr;
  AddNewForegroundWindow(TargetUrl(), &added);
  EXPECT_FALSE(controller()->IsShowing());
  EXPECT_EQ(2, browser()->GetTabStripModel()->count());
}

IN_PROC_BROWSER_TEST_F(AhoiLinkPeekRequestBrowserTest,
                       BlankClicksStayTabsWithoutTheOption) {
  SaveSourceTab();
  SetAutoPeek(false);
  content::WebContents* added = nullptr;
  AddNewForegroundWindow(TargetUrl(), &added);
  EXPECT_FALSE(controller()->IsShowing());
  EXPECT_EQ(2, browser()->GetTabStripModel()->count());
}

IN_PROC_BROWSER_TEST_F(AhoiLinkPeekRequestBrowserTest,
                       BlankClicksOfTemporaryPagesAndSameSiteStayTabs) {
  SetAutoPeek(true);
  content::WebContents* added = nullptr;
  AddNewForegroundWindow(TargetUrl(), &added);
  EXPECT_FALSE(controller()->IsShowing()) << "temporary page";
  EXPECT_EQ(2, browser()->GetTabStripModel()->count());

  browser()->GetTabStripModel()->ActivateTabAt(0);
  SaveSourceTab();
  AddNewForegroundWindow(
      embedded_https_test_server().GetURL("a.com", "/echo?same"), &added);
  EXPECT_FALSE(controller()->IsShowing()) << "same site";
  EXPECT_EQ(3, browser()->GetTabStripModel()->count());
}

}  // namespace ahoi::popup_ui
