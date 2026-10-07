// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <optional>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_content_proxy.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_desktop.h"
#include "chrome/browser/ui/views/test/vertical_tabs_browser_test_mixin.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class ExtensionWorkspaceScopeBrowserTest
    : public VerticalTabsBrowserTestMixin<InProcessBrowserTest> {
 protected:
  void SetUpOnMainThread() override {
    VerticalTabsBrowserTestMixin<InProcessBrowserTest>::SetUpOnMainThread();
    bridge_ = ahoi::SessionBridgeFactory::GetForProfile(browser()->GetProfile());
    ASSERT_TRUE(bridge_);
    ASSERT_TRUE(base::test::RunUntil([&] { return bridge_->is_ready(); }));
    tab_ = browser()->GetTabStripModel()->GetActiveTab();
    ASSERT_TRUE(tab_);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return bridge_->GetWorkspaceForTab(tab_).has_value(); }));
    first_ = bridge_->GetWorkspaceForTab(tab_);
    ASSERT_TRUE(first_);
    second_ = bridge_->CreateWorkspace(u"Empty panel context", u"work",
                                       std::nullopt, true);
    ASSERT_TRUE(second_);
    Select(*first_);
  }

  void TearDownOnMainThread() override {
    tab_ = nullptr;
    bridge_ = nullptr;
    VerticalTabsBrowserTestMixin<InProcessBrowserTest>::TearDownOnMainThread();
  }

  void Select(const base::Uuid& workspace) {
    ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
        browser(), workspace, ahoi::WorkspaceActivationSource::kKeyboard));
    const bool empty = workspace == *second_;
    ASSERT_TRUE(base::test::RunUntil([&] {
      return BrowserView::GetBrowserViewForBrowser(browser())
                 ->multi_contents_view()->IsAhoiEmptyStateVisibleForTesting() ==
             empty;
    }));
    ASSERT_EQ(tab_, browser()->GetTabStripModel()->GetActiveTab());
  }

  void RegisterPanel(bool delayed) {
    ASSERT_TRUE(SidePanelRegistry::From(tab_)->Register(
        std::make_unique<SidePanelEntry>(
            key_, base::BindRepeating(
                      [](bool delayed, int* creations,
                         SidePanelEntryScope&) -> SidePanelNativeView {
                        ++*creations;
                        auto view = std::make_unique<views::View>();
                        view->SetProperty(kSidePanelContentProxyKey,
                                          new SidePanelContentProxy(!delayed));
                        return view;
                      }, delayed, &creations_),
            base::BindRepeating([] { return 360; }))));
  }

  raw_ptr<ahoi::SessionBridge> bridge_ = nullptr;
  raw_ptr<tabs::TabInterface> tab_ = nullptr;
  std::optional<base::Uuid> first_;
  std::optional<base::Uuid> second_;
  const SidePanelEntryKey key_{SidePanelEntryId::kBookmarks};
  int creations_ = 0;
};

IN_PROC_BROWSER_TEST_F(ExtensionWorkspaceScopeBrowserTest,
                       EmptyWorkspaceDoesNotExposePreviousTabContext) {
  content::WebContents* contents = tab_->GetContents();
  const GURL original_url = contents->GetLastCommittedURL();
  auto* extensions = BrowserView::GetBrowserViewForBrowser(browser())
                         ->toolbar()->extensions_container();
  ASSERT_TRUE(extensions);
  ASSERT_EQ(contents, extensions->GetCurrentWebContents());
  RegisterPanel(false);
  auto* panels = browser()->GetFeatures().side_panel_ui();
  ASSERT_TRUE(panels);
  panels->Show(key_, std::nullopt, true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return panels->IsSidePanelEntryShowing(key_, true); }));
  ASSERT_EQ(1, creations_);

  Select(*second_);
  EXPECT_EQ(nullptr, extensions->GetCurrentWebContents());
  EXPECT_FALSE(panels->IsSidePanelEntryShowing(key_, true));
  EXPECT_EQ(contents, tab_->GetContents());
  EXPECT_EQ(original_url, contents->GetLastCommittedURL());

  Select(*first_);
  EXPECT_EQ(contents, extensions->GetCurrentWebContents());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return panels->IsSidePanelEntryShowing(key_, true); }));
  EXPECT_EQ(1, creations_);  // Native registry reuses the cached panel view.
}

IN_PROC_BROWSER_TEST_F(ExtensionWorkspaceScopeBrowserTest,
                       LatePanelLoadDoesNotExposeHiddenWorkspace) {
  RegisterPanel(true);
  auto* panels = browser()->GetFeatures().side_panel_ui();
  ASSERT_TRUE(panels);
  panels->Show(key_, std::nullopt, true);
  ASSERT_EQ(1, creations_);
  ASSERT_FALSE(panels->IsSidePanelEntryShowing(key_, true));
  Select(*second_);
  auto* entry = SidePanelRegistry::From(tab_)->GetEntryForKey(key_);
  ASSERT_TRUE(entry);
  ASSERT_TRUE(entry->CachedView());
  entry->CachedView()->GetProperty(kSidePanelContentProxyKey)->SetAvailable(true);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(panels->IsSidePanelEntryShowing(key_, true));
}
