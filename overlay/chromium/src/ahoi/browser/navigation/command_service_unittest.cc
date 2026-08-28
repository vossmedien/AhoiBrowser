// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/command_service.h"

#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "components/omnibox/browser/autocomplete_scheme_classifier.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

class CommandTestSchemeClassifier final : public AutocompleteSchemeClassifier {
 public:
  metrics::OmniboxInputType GetInputTypeForScheme(
      const std::string& scheme) const override {
    if (scheme == "about" || scheme == "chrome" || scheme == "file" ||
        scheme == "view-source") {
      return metrics::OmniboxInputType::URL;
    }
    return metrics::OmniboxInputType::EMPTY;
  }
};

CommandItem MakeItem(CommandItemType type,
                     std::string stable_id,
                     std::u16string title,
                     int priority = 0) {
  CommandItem item;
  item.type = type;
  item.stable_id = std::move(stable_id);
  item.title = std::move(title);
  item.priority = priority;
  if (type == CommandItemType::kOpenTab ||
      type == CommandItemType::kSavedPage ||
      type == CommandItemType::kDeviceTab ||
      type == CommandItemType::kHistory) {
    item.url = GURL("https://example.test/" + item.stable_id);
  }
  return item;
}

TEST(CommandServiceTest, ParsesSearchShortcutAndUrlsLocally) {
  CommandService service;
  CommandTestSchemeClassifier scheme_classifier;

  ParsedCommandInput parsed =
      service.ParseInput(u"g chromium source", scheme_classifier);
  EXPECT_EQ(parsed.kind, CommandInputKind::kSearchShortcut);
  EXPECT_EQ(parsed.search_shortcut, u"g");
  EXPECT_EQ(parsed.text, u"chromium source");

  parsed = service.ParseInput(u"https://example.test/a", scheme_classifier);
  EXPECT_EQ(parsed.kind, CommandInputKind::kUrl);
  EXPECT_EQ(parsed.url, GURL("https://example.test/a"));

  parsed = service.ParseInput(u"example.com/a", scheme_classifier);
  EXPECT_EQ(parsed.kind, CommandInputKind::kUrl);
  EXPECT_EQ(parsed.url, GURL("https://example.com/a"));

  parsed = service.ParseInput(u"fpn-dichtstoffe.de", scheme_classifier);
  EXPECT_EQ(parsed.kind, CommandInputKind::kUrl);
  EXPECT_EQ(parsed.url, GURL("https://fpn-dichtstoffe.de/"));

  parsed = service.ParseInput(u"localhost:8443/path", scheme_classifier);
  EXPECT_EQ(parsed.kind, CommandInputKind::kUrl);
  EXPECT_EQ(parsed.url, GURL("http://localhost:8443/path"));

  parsed = service.ParseInput(u"chrome://version", scheme_classifier);
  EXPECT_EQ(parsed.kind, CommandInputKind::kUrl);
  EXPECT_EQ(parsed.url, GURL("chrome://version/"));

  parsed = service.ParseInput(u"1.2", scheme_classifier);
  EXPECT_EQ(parsed.kind, CommandInputKind::kLocalOrSearch);

  parsed = service.ParseInput(u"localhostish", scheme_classifier);
  EXPECT_EQ(parsed.kind, CommandInputKind::kLocalOrSearch);

  parsed = service.ParseInput(u"ordinary search words", scheme_classifier);
  EXPECT_EQ(parsed.kind, CommandInputKind::kLocalOrSearch);
}

TEST(CommandServiceTest, RanksExactAndPrefixMatchesDeterministically) {
  CommandService service;
  ASSERT_TRUE(service.ReplaceItems(
      CommandItemType::kWorkspace,
      {MakeItem(CommandItemType::kWorkspace, "workspace-docs", u"Docs"),
       MakeItem(CommandItemType::kWorkspace, "workspace-docker", u"Docker")}));
  ASSERT_TRUE(
      service.ReplaceItems(CommandItemType::kSavedPage,
                           {MakeItem(CommandItemType::kSavedPage, "page-docs",
                                     u"Docs Chromium", 20)}));

  const std::vector<RankedCommand> results = service.Query(u"docs", 10u);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].item.stable_id, "workspace-docs");
  EXPECT_EQ(results[1].item.stable_id, "page-docs");
  EXPECT_GT(results[0].score, results[1].score);
}

TEST(CommandServiceTest, InvalidReplacementLeavesPreviousSourceIntact) {
  CommandService service;
  const CommandItem original =
      MakeItem(CommandItemType::kBrowserCommand, "reload", u"Reload");
  ASSERT_TRUE(
      service.ReplaceItems(CommandItemType::kBrowserCommand, {original}));

  CommandItem duplicate =
      MakeItem(CommandItemType::kBrowserCommand, "reload", u"Reload hard");
  EXPECT_FALSE(service.ReplaceItems(CommandItemType::kBrowserCommand,
                                    {original, duplicate}));
  const std::vector<RankedCommand> results = service.Query(u"reload", 10u);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].item.title, u"Reload");
}

TEST(CommandServiceTest, RejectsTypeUrlShapeMismatchAtomically) {
  CommandService service;
  const CommandItem original =
      MakeItem(CommandItemType::kSavedPage, "original", u"Original");
  ASSERT_TRUE(service.ReplaceItems(CommandItemType::kSavedPage, {original}));

  CommandItem invalid =
      MakeItem(CommandItemType::kSavedPage, "invalid", u"Invalid");
  invalid.url.reset();
  EXPECT_FALSE(service.ReplaceItems(CommandItemType::kSavedPage, {invalid}));

  invalid = MakeItem(CommandItemType::kSavedPage, "blank", u" \t ");
  EXPECT_FALSE(service.ReplaceItems(CommandItemType::kSavedPage, {invalid}));

  const std::vector<RankedCommand> results = service.Query(u"original", 10u);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].item.stable_id, "original");
}

TEST(CommandServiceTest, RejectsUrlUserinfoBeforeIndexing) {
  CommandService service;
  CommandItem credential_bearing =
      MakeItem(CommandItemType::kSavedPage, "private", u"Private");
  credential_bearing.url =
      GURL("https://username:password@example.test/private");
  credential_bearing.keywords = {u"username", u"password"};

  EXPECT_FALSE(service.ReplaceItems(CommandItemType::kSavedPage,
                                    {std::move(credential_bearing)}));
  EXPECT_TRUE(service.Query(u"password", 10u).empty());
}

TEST(CommandServiceTest, LimitsResultsAndUsesRecencyAsStableTieBreaker) {
  CommandService service;
  CommandItem older = MakeItem(CommandItemType::kOpenTab, "older", u"Project");
  older.last_used = base::Time::UnixEpoch() + base::Seconds(1);
  CommandItem newer = MakeItem(CommandItemType::kOpenTab, "newer", u"Project");
  newer.last_used = base::Time::UnixEpoch() + base::Seconds(2);
  ASSERT_TRUE(service.ReplaceItems(CommandItemType::kOpenTab,
                                   {std::move(older), std::move(newer)}));

  const std::vector<RankedCommand> results = service.Query(u"project", 1u);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].item.stable_id, "newer");
}

TEST(CommandServiceTest, CrossSourceTiesHaveTotalDeterministicOrder) {
  CommandService service;
  CommandItem open = MakeItem(CommandItemType::kOpenTab, "same", u"Project");
  CommandItem saved = MakeItem(CommandItemType::kSavedPage, "same", u"Project");
  saved.url = GURL("https://example.test/saved-same");
  ASSERT_TRUE(
      service.ReplaceItems(CommandItemType::kOpenTab, {std::move(open)}));
  ASSERT_TRUE(
      service.ReplaceItems(CommandItemType::kSavedPage, {std::move(saved)}));

  const std::vector<RankedCommand> results = service.Query(u"project", 10u);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].item.type, CommandItemType::kOpenTab);
  EXPECT_EQ(results[1].item.type, CommandItemType::kSavedPage);
}

TEST(CommandServiceTest, ExplicitPrefixesScopeLocallyAndNeverBecomeSearches) {
  CommandService service;
  CommandTestSchemeClassifier scheme_classifier;
  ASSERT_TRUE(service.ReplaceItems(
      CommandItemType::kOpenTab,
      {MakeItem(CommandItemType::kOpenTab, "tab-docs", u"Docs tab")}));
  ASSERT_TRUE(service.ReplaceItems(
      CommandItemType::kHistory,
      {MakeItem(CommandItemType::kHistory, "history-docs", u"Docs history")}));
  ASSERT_TRUE(service.ReplaceItems(
      CommandItemType::kFolder,
      {MakeItem(CommandItemType::kFolder, "folder-docs", u"Docs folder")}));
  ASSERT_TRUE(
      service.ReplaceItems(CommandItemType::kBrowserCommand,
                           {MakeItem(CommandItemType::kBrowserCommand,
                                     "browser.reload", u"Reload page")}));

  EXPECT_EQ(service.Query(u"@tabs docs", 10u).front().item.type,
            CommandItemType::kOpenTab);
  EXPECT_EQ(service.Query(u"@history docs", 10u).front().item.type,
            CommandItemType::kHistory);
  EXPECT_EQ(service.Query(u"@tree docs", 10u).front().item.type,
            CommandItemType::kFolder);
  EXPECT_EQ(service.Query(u">reload", 10u).front().item.type,
            CommandItemType::kBrowserCommand);
  EXPECT_TRUE(service.IsExplicitLocalQuery(u"@tabs"));
  EXPECT_TRUE(service.IsExplicitLocalQuery(u"> reload"));
  EXPECT_FALSE(service.IsExplicitLocalQuery(u"ordinary query"));
  EXPECT_EQ(service.ParseInput(u"@history chromium", scheme_classifier).kind,
            CommandInputKind::kLocalOnly);
}

TEST(CommandServiceTest, QueryDeduplicatesUrlAcrossAllLocalSources) {
  CommandService service;
  CommandItem open =
      MakeItem(CommandItemType::kOpenTab, "tab", u"Project open", 300);
  CommandItem saved =
      MakeItem(CommandItemType::kSavedPage, "saved", u"Project saved", 120);
  CommandItem history =
      MakeItem(CommandItemType::kHistory, "history", u"Project history", 40);
  saved.url = open.url;
  history.url = open.url;
  ASSERT_TRUE(service.ReplaceItems(CommandItemType::kOpenTab, {open}));
  ASSERT_TRUE(service.ReplaceItems(CommandItemType::kSavedPage, {saved}));
  ASSERT_TRUE(service.ReplaceItems(CommandItemType::kHistory, {history}));

  const std::vector<RankedCommand> results = service.Query(u"project", 10u);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results.front().item.type, CommandItemType::kOpenTab);
}

TEST(CommandServiceTest, QueryPolicyKeepsDuplicateTabsAndFiltersSources) {
  CommandService service;
  const GURL shared_url("https://example.test/shared");
  CommandItem first =
      MakeItem(CommandItemType::kOpenTab, "first", u"Shared first");
  first.url = shared_url;
  CommandItem second =
      MakeItem(CommandItemType::kOpenTab, "second", u"Shared second");
  second.url = shared_url;
  CommandItem saved =
      MakeItem(CommandItemType::kSavedPage, "saved", u"Shared saved");
  saved.url = shared_url;
  CommandItem history =
      MakeItem(CommandItemType::kHistory, "history", u"Shared history");
  history.url = shared_url;
  ASSERT_TRUE(service.ReplaceItems(CommandItemType::kOpenTab,
                                   {std::move(first), std::move(second)}));
  ASSERT_TRUE(
      service.ReplaceItems(CommandItemType::kSavedPage, {std::move(saved)}));
  ASSERT_TRUE(
      service.ReplaceItems(CommandItemType::kHistory, {std::move(history)}));

  const CommandQueryOptions sidebar_options{
      .allowed_types = {CommandItemType::kOpenTab, CommandItemType::kSavedPage},
      .deduplicate_urls = false,
      .max_results = 10,
  };
  const std::vector<RankedCommand> results =
      service.Query(u"shared", sidebar_options);
  ASSERT_EQ(results.size(), 3u);
  EXPECT_EQ(results[0].item.type, CommandItemType::kOpenTab);
  EXPECT_EQ(results[1].item.type, CommandItemType::kOpenTab);
  EXPECT_NE(results[0].item.stable_id, results[1].item.stable_id);
  EXPECT_EQ(results[2].item.type, CommandItemType::kSavedPage);
}

TEST(CommandServiceTest, DeviceTabsRequireAnExplicitSurfacePolicy) {
  CommandService service;
  ASSERT_TRUE(service.ReplaceItems(
      CommandItemType::kDeviceTab,
      {MakeItem(CommandItemType::kDeviceTab, "device:tab", u"Remote docs")}));

  EXPECT_TRUE(service.Query(u"remote", 10u).empty());

  const CommandQueryOptions sidebar_options{
      .allowed_types = {CommandItemType::kDeviceTab},
      .deduplicate_urls = false,
      .max_results = 10u,
  };
  const std::vector<RankedCommand> results =
      service.Query(u"@tabs remote", sidebar_options);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results.front().item.type, CommandItemType::kDeviceTab);
  EXPECT_EQ(results.front().item.stable_id, "device:tab");
}

TEST(CommandServiceTest, TenThousandItemIndexReturnsBoundedResults) {
  CommandService service;
  std::vector<CommandItem> items;
  items.reserve(10000u);
  for (int i = 0; i < 10000; ++i) {
    items.push_back(MakeItem(CommandItemType::kSavedPage,
                             "page-" + base::NumberToString(i),
                             u"Project " + base::NumberToString16(i)));
  }
  ASSERT_TRUE(
      service.ReplaceItems(CommandItemType::kSavedPage, std::move(items)));

  const std::vector<RankedCommand> results = service.Query(u"project", 20u);
  EXPECT_EQ(results.size(), 20u);
}

}  // namespace
}  // namespace ahoi
