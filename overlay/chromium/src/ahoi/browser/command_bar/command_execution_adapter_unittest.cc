// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/command_execution_adapter.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/command_bar/command_execution_adapter_internal.h"
#include "ahoi/browser/navigation/command_service.h"
#include "base/memory/raw_ptr.h"
#include "components/omnibox/browser/test_scheme_classifier.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {

namespace {

class FakeSearchResolver final : public CommandBarSearchResolver {
 public:
  std::u16string GetEngineName() const override { return u"Local Search"; }

  std::optional<ResolvedCommandBarSearch> Resolve(
      std::u16string_view terms) const override {
    last_terms = std::u16string(terms);
    if (terms.empty()) {
      return std::nullopt;
    }
    return ResolvedCommandBarSearch{
        .url = GURL("https://search.test/results"),
        .engine_name = u"Local Search",
        .content_type = "application/x-www-form-urlencoded",
        .post_data = "q=test",
    };
  }

  mutable std::u16string last_terms;
};

class FakeExecutionDelegate final : public CommandExecutionDelegate {
 public:
  bool ActivateOpenTab(const CommandItem& item) override {
    activated_item = item;
    return activate_open_tab_result;
  }

  bool Navigate(const GURL& url,
                std::string_view content_type,
                std::string_view post_data,
                CommandBarDisposition disposition,
                bool is_search,
                CommandBarNavigationHints navigation_hints) override {
    navigated_url = url;
    navigated_content_type = std::string(content_type);
    navigated_post_data = std::string(post_data);
    navigated_disposition = disposition;
    navigated_is_search = is_search;
    navigated_navigation_hints = navigation_hints;
    ++navigate_count;
    return true;
  }

  bool SwitchWorkspace(std::string_view stable_id) override {
    switched_workspace = std::string(stable_id);
    return true;
  }

  bool CanRevealFolder(std::string_view stable_id) const override {
    return folder_reveal_enabled;
  }

  bool RevealFolder(std::string_view stable_id) override {
    revealed_folder = std::string(stable_id);
    return true;
  }

  bool CanExecuteBrowserCommand(std::string_view stable_id) const override {
    return browser_command_enabled;
  }

  bool ExecuteBrowserCommand(std::string_view stable_id) override {
    executed_browser_command = std::string(stable_id);
    return true;
  }

  bool CanExecuteDeveloperAction(DeveloperAction /*action*/) const override {
    return developer_action_enabled;
  }

  bool ExecuteDeveloperAction(DeveloperAction action) override {
    executed_developer_action = action;
    return true;
  }

  bool CanMoveToWorkspace(std::string_view workspace_id) const override {
    return move_enabled;
  }

  bool MoveToWorkspace(std::string_view workspace_id) override {
    moved_to_workspace = std::string(workspace_id);
    return true;
  }

  bool CanMergeWorkspaceInto(std::string_view workspace_id) const override {
    return merge_enabled;
  }

  bool MergeWorkspaceInto(std::string_view workspace_id) override {
    merged_into_workspace = std::string(workspace_id);
    return true;
  }

  bool activate_open_tab_result = false;
  std::optional<CommandItem> activated_item;
  GURL navigated_url;
  std::string navigated_content_type;
  std::string navigated_post_data;
  CommandBarDisposition navigated_disposition =
      CommandBarDisposition::kCurrentTab;
  bool navigated_is_search = false;
  CommandBarNavigationHints navigated_navigation_hints;
  int navigate_count = 0;
  std::string switched_workspace;
  std::string revealed_folder;
  bool browser_command_enabled = true;
  bool folder_reveal_enabled = true;
  std::string executed_browser_command;
  bool developer_action_enabled = true;
  std::optional<DeveloperAction> executed_developer_action;
  bool move_enabled = true;
  std::string moved_to_workspace;
  bool merge_enabled = true;
  std::string merged_into_workspace;
};

class CommandExecutionAdapterTest : public testing::Test {
 public:
  void SetUp() override {
    auto search_resolver = std::make_unique<FakeSearchResolver>();
    search_resolver_ = search_resolver.get();
    auto execution_delegate = std::make_unique<FakeExecutionDelegate>();
    execution_delegate_ = execution_delegate.get();
    adapter_ = std::make_unique<CommandExecutionAdapter>(
        &command_service_, std::make_unique<TestSchemeClassifier>(),
        std::move(search_resolver), std::move(execution_delegate));
  }

 protected:
  CommandService command_service_;
  raw_ptr<FakeSearchResolver> search_resolver_ = nullptr;
  raw_ptr<FakeExecutionDelegate> execution_delegate_ = nullptr;
  std::unique_ptr<CommandExecutionAdapter> adapter_;
};

TEST_F(CommandExecutionAdapterTest, DirectUrlUsesRequestedDisposition) {
  EXPECT_TRUE(adapter_->ExecuteInput(u"https://example.test/path",
                                     CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->navigated_url,
            GURL("https://example.test/path"));
  EXPECT_EQ(execution_delegate_->navigated_disposition,
            CommandBarDisposition::kCurrentTab);
  EXPECT_FALSE(execution_delegate_->navigated_is_search);
  EXPECT_FALSE(execution_delegate_->navigated_navigation_hints
                   .is_using_https_as_default_scheme);
  EXPECT_FALSE(execution_delegate_->navigated_navigation_hints
                   .url_typed_with_http_scheme);
  EXPECT_TRUE(search_resolver_->last_terms.empty());
}

TEST_F(CommandExecutionAdapterTest, ExplicitHttpUrlDisablesHttpsFirstUpgrade) {
  EXPECT_TRUE(adapter_->ExecuteInput(u"http://127.0.0.1:8765/title2.html",
                                     CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->navigated_url,
            GURL("http://127.0.0.1:8765/title2.html"));
  EXPECT_FALSE(execution_delegate_->navigated_navigation_hints
                   .is_using_https_as_default_scheme);
  EXPECT_TRUE(execution_delegate_->navigated_navigation_hints
                  .url_typed_with_http_scheme);
}

TEST_F(CommandExecutionAdapterTest,
       BareHttpsUrlPreservesDefaultSchemeMetadata) {
  EXPECT_TRUE(adapter_->ExecuteInput(u"example.com/path",
                                     CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->navigated_url,
            GURL("https://example.com/path"));
  EXPECT_TRUE(execution_delegate_->navigated_navigation_hints
                  .is_using_https_as_default_scheme);
  EXPECT_FALSE(execution_delegate_->navigated_navigation_hints
                   .url_typed_with_http_scheme);
}

TEST_F(CommandExecutionAdapterTest,
       GShortcutUsesDefaultSearchWithoutSuggestions) {
  EXPECT_TRUE(adapter_->ExecuteInput(u"g privacy browser",
                                     CommandBarDisposition::kNewForegroundTab));
  EXPECT_EQ(search_resolver_->last_terms, u"privacy browser");
  EXPECT_EQ(execution_delegate_->navigated_url,
            GURL("https://search.test/results"));
  EXPECT_EQ(execution_delegate_->navigated_content_type,
            "application/x-www-form-urlencoded");
  EXPECT_EQ(execution_delegate_->navigated_post_data, "q=test");
  EXPECT_EQ(execution_delegate_->navigated_disposition,
            CommandBarDisposition::kNewForegroundTab);
  EXPECT_TRUE(execution_delegate_->navigated_is_search);
}

TEST_F(CommandExecutionAdapterTest, PlainQueryUsesConfiguredDefaultSearch) {
  EXPECT_TRUE(adapter_->ExecuteInput(u"privacy browser",
                                     CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(search_resolver_->last_terms, u"privacy browser");
  EXPECT_EQ(execution_delegate_->navigated_url,
            GURL("https://search.test/results"));
  EXPECT_EQ(execution_delegate_->navigated_disposition,
            CommandBarDisposition::kCurrentTab);
  EXPECT_TRUE(execution_delegate_->navigated_is_search);
}

TEST_F(CommandExecutionAdapterTest, PreviewIsPresentationReady) {
  const std::optional<CommandBarSuggestion> url =
      adapter_->PreviewInput(u"https://example.test/");
  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url->kind, CommandBarSuggestionKind::kInputFallback);
  EXPECT_EQ(url->title, u"https://example.test/");
  EXPECT_EQ(url->secondary_text, u"https://example.test/");
  EXPECT_EQ(url->destination_url, GURL("https://example.test/"));

  const std::optional<CommandBarSuggestion> search =
      adapter_->PreviewInput(u"g local only");
  ASSERT_TRUE(search.has_value());
  EXPECT_EQ(search->title, u"local only");
  EXPECT_EQ(search->secondary_text, u"Local Search");
  EXPECT_FALSE(search->destination_url.has_value());
}

TEST_F(CommandExecutionAdapterTest, OpenTabActivatesBeforeFallingBackToUrl) {
  CommandItem item{
      .type = CommandItemType::kOpenTab,
      .stable_id = "tab-1",
      .title = u"Existing tab",
      .url = GURL("https://existing.test/"),
  };

  execution_delegate_->activate_open_tab_result = true;
  EXPECT_TRUE(adapter_->ExecuteItem(item, CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->navigate_count, 0);

  execution_delegate_->activate_open_tab_result = false;
  EXPECT_TRUE(
      adapter_->ExecuteItem(item, CommandBarDisposition::kNewForegroundTab));
  EXPECT_EQ(execution_delegate_->navigate_count, 1);
  EXPECT_EQ(execution_delegate_->navigated_url, *item.url);
}

TEST_F(CommandExecutionAdapterTest, BrowserCommandsAreExplicitlyAllowlisted) {
  CommandItem allowed{
      .type = CommandItemType::kBrowserCommand,
      .stable_id = "browser.reload",
      .title = u"Reload",
  };
  EXPECT_TRUE(adapter_->CanExecuteItem(allowed));
  EXPECT_TRUE(
      adapter_->ExecuteItem(allowed, CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->executed_browser_command, "browser.reload");

  allowed.stable_id = "browser.unreviewed-command";
  EXPECT_FALSE(adapter_->CanExecuteItem(allowed));
  EXPECT_FALSE(
      adapter_->ExecuteItem(allowed, CommandBarDisposition::kCurrentTab));

  allowed.stable_id = "browser.reload";
  execution_delegate_->browser_command_enabled = false;
  EXPECT_FALSE(adapter_->CanExecuteItem(allowed));
  EXPECT_FALSE(
      adapter_->ExecuteItem(allowed, CommandBarDisposition::kCurrentTab));
}

TEST_F(CommandExecutionAdapterTest, HttpAuthManagerUsesReviewedNativeSurface) {
  CommandItem item{
      .type = CommandItemType::kBrowserCommand,
      .stable_id = "http-auth.manage",
      .title = u"Manage HTTP authentication",
  };
  EXPECT_TRUE(adapter_->CanExecuteItem(item));
  EXPECT_TRUE(adapter_->ExecuteItem(item, CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->executed_browser_command, "http-auth.manage");
}

TEST_F(CommandExecutionAdapterTest,
       DeveloperCommandsAreExplicitlyAllowlistedAndTargetGated) {
  CommandItem item{
      .type = CommandItemType::kBrowserCommand,
      .stable_id = "developer.toggle-css",
      .title = u"Toggle CSS",
  };
  EXPECT_TRUE(adapter_->CanExecuteItem(item));
  EXPECT_TRUE(adapter_->ExecuteItem(item, CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->executed_developer_action,
            DeveloperAction::kToggleCss);

  item.stable_id = "developer.arbitrary-script";
  EXPECT_FALSE(adapter_->CanExecuteItem(item));
  EXPECT_FALSE(adapter_->ExecuteItem(item, CommandBarDisposition::kCurrentTab));

  item.stable_id = "developer.clear-site-cache";
  execution_delegate_->developer_action_enabled = false;
  EXPECT_FALSE(adapter_->CanExecuteItem(item));
}

TEST_F(CommandExecutionAdapterTest, FolderAndWorkspaceAreExecutable) {
  CommandItem folder{
      .type = CommandItemType::kFolder,
      .stable_id = "d3394e7a-2144-466e-946b-d683d6a66cba",
      .title = u"Project",
  };
  CommandItem workspace{
      .type = CommandItemType::kWorkspace,
      .stable_id = "63f5609d-fffa-4d2f-b68d-5c86c128b56e",
      .title = u"Work",
  };
  EXPECT_TRUE(adapter_->CanExecuteItem(folder));
  EXPECT_TRUE(
      adapter_->ExecuteItem(folder, CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->revealed_folder, folder.stable_id);
  EXPECT_TRUE(adapter_->CanExecuteItem(workspace));
  EXPECT_TRUE(
      adapter_->ExecuteItem(workspace, CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->switched_workspace, workspace.stable_id);
}

// ADR 0012 section 2 / CMD-MOVE-01.
TEST_F(CommandExecutionAdapterTest, MoveToWorkspaceItemsRouteToTheDelegate) {
  const base::Uuid work = base::Uuid::GenerateRandomV4();
  const std::vector<CommandItem> items =
      internal::BuildMoveToWorkspaceCommands(
          {{.id = work, .name = u"Work"},
           {.id = base::Uuid(), .name = u"Invalid"},
           {.id = base::Uuid::GenerateRandomV4(), .name = u""}},
          /*german=*/true);
  ASSERT_EQ(items.size(), 1u);
  const CommandItem& item = items.front();
  EXPECT_EQ(item.type, CommandItemType::kBrowserCommand);
  EXPECT_EQ(item.stable_id, "move-to-workspace." + work.AsLowercaseString());
  EXPECT_EQ(item.title, u"In Workspace verschieben: Work");
  EXPECT_NE(std::ranges::find(item.keywords, u"Work"), item.keywords.end());
  EXPECT_NE(std::ranges::find(item.keywords, u"move to workspace"),
            item.keywords.end());
  EXPECT_EQ(internal::BuildMoveToWorkspaceCommands(
                {{.id = work, .name = u"Work"}}, /*german=*/false)
                .front()
                .title,
            u"Move to Workspace: Work");
  EXPECT_TRUE(item.secondary_text.empty());
  // WS-ISO-05: a Workspace of another Profile says that sign-ins stay.
  EXPECT_EQ(internal::BuildMoveToWorkspaceCommands(
                {{.id = work, .name = u"Work", .separate_sign_ins = true}},
                /*german=*/true)
                .front()
                .secondary_text,
            u"Anmeldungen ziehen nicht mit");
  // The index accepts the items next to the other browser commands.
  EXPECT_TRUE(command_service_.ReplaceItems(CommandItemType::kBrowserCommand,
                                            items));

  EXPECT_TRUE(adapter_->CanExecuteItem(item));
  EXPECT_TRUE(adapter_->ExecuteItem(item, CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->moved_to_workspace, work.AsLowercaseString());
  EXPECT_TRUE(execution_delegate_->executed_browser_command.empty());

  // The delegate refuses, e.g. the window's own Workspace.
  execution_delegate_->move_enabled = false;
  EXPECT_FALSE(adapter_->CanExecuteItem(item));
  EXPECT_FALSE(adapter_->ExecuteItem(item, CommandBarDisposition::kCurrentTab));

  // A malformed id never reaches the delegate or chrome::ExecuteCommand.
  execution_delegate_->move_enabled = true;
  CommandItem malformed = item;
  malformed.stable_id = "move-to-workspace.not-a-uuid";
  EXPECT_FALSE(adapter_->CanExecuteItem(malformed));
  EXPECT_FALSE(
      adapter_->ExecuteItem(malformed, CommandBarDisposition::kCurrentTab));
}

// ADR 0012 section 1: "Zusammenführen mit …" in the command bar opens the
// Workspace menu's merge dialog through the delegate, never a browser command.
TEST_F(CommandExecutionAdapterTest, MergeWorkspaceItemsRouteToTheDelegate) {
  const base::Uuid home = base::Uuid::GenerateRandomV4();
  const std::vector<CommandItem> items = internal::BuildMergeWorkspaceCommands(
      {{.id = home, .name = u"Zuhause"},
       {.id = base::Uuid(), .name = u"Invalid"},
       {.id = base::Uuid::GenerateRandomV4(), .name = u""}},
      /*german=*/true);
  ASSERT_EQ(items.size(), 1u);
  const CommandItem& item = items.front();
  EXPECT_EQ(item.type, CommandItemType::kBrowserCommand);
  EXPECT_EQ(item.stable_id, "merge-workspace." + home.AsLowercaseString());
  EXPECT_EQ(item.title, u"Zusammenführen mit: Zuhause");
  for (const std::u16string_view keyword :
       {u"Zuhause", u"zusammenführen mit", u"merge into"}) {
    EXPECT_NE(std::ranges::find(item.keywords, keyword), item.keywords.end());
  }
  EXPECT_EQ(internal::BuildMergeWorkspaceCommands(
                {{.id = home, .name = u"Home"}}, /*german=*/false)
                .front()
                .title,
            u"Merge into: Home");

  // Next to the move items for the same target, with a distinct id.
  std::vector<CommandItem> all = internal::BuildMoveToWorkspaceCommands(
      {{.id = home, .name = u"Zuhause"}}, /*german=*/true);
  all.push_back(item);
  EXPECT_NE(all.front().stable_id, all.back().stable_id);
  EXPECT_TRUE(
      command_service_.ReplaceItems(CommandItemType::kBrowserCommand, all));
  EXPECT_FALSE(internal::GetMoveToWorkspaceTarget(item.stable_id));
  EXPECT_FALSE(internal::GetMergeWorkspaceTarget(all.front().stable_id));

  EXPECT_TRUE(adapter_->CanExecuteItem(item));
  EXPECT_TRUE(adapter_->ExecuteItem(item, CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->merged_into_workspace,
            home.AsLowercaseString());
  EXPECT_TRUE(execution_delegate_->moved_to_workspace.empty());
  EXPECT_TRUE(execution_delegate_->executed_browser_command.empty());

  // The sidebar refuses, e.g. the window's own Workspace or an open dialog.
  execution_delegate_->merge_enabled = false;
  execution_delegate_->merged_into_workspace.clear();
  EXPECT_FALSE(adapter_->CanExecuteItem(item));
  EXPECT_FALSE(adapter_->ExecuteItem(item, CommandBarDisposition::kCurrentTab));
  EXPECT_TRUE(execution_delegate_->merged_into_workspace.empty());

  // A malformed id never reaches the delegate or chrome::ExecuteCommand.
  execution_delegate_->merge_enabled = true;
  CommandItem malformed = item;
  malformed.stable_id = "merge-workspace.NOT-A-UUID";
  EXPECT_FALSE(adapter_->CanExecuteItem(malformed));
  EXPECT_FALSE(
      adapter_->ExecuteItem(malformed, CommandBarDisposition::kCurrentTab));
  EXPECT_TRUE(execution_delegate_->merged_into_workspace.empty());
  EXPECT_TRUE(execution_delegate_->executed_browser_command.empty());
}

TEST_F(CommandExecutionAdapterTest, ExplicitLocalPrefixHasNoSearchFallback) {
  EXPECT_FALSE(adapter_->PreviewInput(u"@history project").has_value());
  EXPECT_FALSE(adapter_->PreviewInput(u">reload").has_value());
  EXPECT_FALSE(adapter_->ExecuteInput(u"@tree docs",
                                      CommandBarDisposition::kCurrentTab));
  EXPECT_EQ(execution_delegate_->navigate_count, 0);
}

TEST_F(CommandExecutionAdapterTest, DailyDriverCommandsStayAllowlisted) {
  constexpr std::string_view kCommands[] = {
      "browser.reload",
      "browser.reload-bypassing-cache",
      "browser.downloads",
      "browser.history",
      "browser.settings",
      "browser.clear-browsing-data",
      "browser.devtools",
      "browser.view-source",
      "browser.print",
      "browser.new-window",
      "browser.new-incognito-window",
      "browser.open-in-normal-window",
      "privacy.open",
      "extensions.ubo-classic",
      "http-auth.switch",
      "http-auth.forget",
      "http-auth.manage",
      "page.copy-link",
      "page.copy-markdown-link",
      "page.reading-mode",
      "developer.clear-site-cache",
      "developer.toggle-css",
      "developer.reveal-passwords",
      "developer.reset-page",
      "developer.toggle-javascript",
      "developer.toggle-images",
      "developer.screenshot-visible",
      "developer.screenshot-full-page",
  };
  for (const std::string_view stable_id : kCommands) {
    CommandItem item{
        .type = CommandItemType::kBrowserCommand,
        .stable_id = std::string(stable_id),
        .title = u"Reviewed command",
    };
    EXPECT_TRUE(adapter_->CanExecuteItem(item)) << stable_id;
  }
}

}  // namespace
}  // namespace ahoi
