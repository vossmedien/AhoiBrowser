// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/command_bar_types.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {

TEST(CommandBarTypesTest, KeepsRichestSuggestionForDuplicateTypedUrl) {
  const GURL destination("https://example.test/");
  std::vector<CommandBarSuggestion> suggestions = {
      {.kind = CommandBarSuggestionKind::kLocalItem,
       .title = u"Example Domain",
       .item = CommandItem{.type = CommandItemType::kHistory,
                           .stable_id = "history:example",
                           .title = u"Example Domain",
                           .url = destination},
       .destination_url = destination},
      {.kind = CommandBarSuggestionKind::kInputFallback,
       .title = u"example.test",
       .destination_url = destination},
      {.kind = CommandBarSuggestionKind::kInputFallback,
       .title = u"Search for example",
       .secondary_text = u"Example Search"},
  };

  suggestions = DeduplicateSuggestionsByDestination(std::move(suggestions));

  ASSERT_EQ(suggestions.size(), 2u);
  EXPECT_EQ(suggestions[0].title, u"Example Domain");
  EXPECT_EQ(suggestions[1].title, u"Search for example");
}

TEST(CommandBarTypesTest, TypedHostPrecedesUnrelatedFuzzyHistoryMatches) {
  const GURL typed_destination("https://fpn-dichtstoffe.de/");
  std::vector<CommandBarSuggestion> suggestions = {
      {.kind = CommandBarSuggestionKind::kLocalItem,
       .title = u"WinFuture",
       .destination_url = GURL("https://winfuture.de/")},
      {.kind = CommandBarSuggestionKind::kLocalItem,
       .title = u"4Players",
       .destination_url = GURL("https://www.4players.de/")},
  };

  suggestions = MergeCommandBarSuggestions(
      std::move(suggestions),
      CommandBarSuggestion{
          .kind = CommandBarSuggestionKind::kInputFallback,
          .title = u"fpn-dichtstoffe.de",
          .destination_url = typed_destination,
      },
      5u);

  ASSERT_EQ(3u, suggestions.size());
  EXPECT_EQ(CommandBarSuggestionKind::kInputFallback, suggestions.front().kind);
  EXPECT_EQ(typed_destination, suggestions.front().destination_url);
}

TEST(CommandBarTypesTest, ExactLocalDestinationRepresentsTypedHostFirst) {
  const GURL typed_destination("https://fpn-dichtstoffe.de/");
  std::vector<CommandBarSuggestion> suggestions = {
      {.kind = CommandBarSuggestionKind::kLocalItem,
       .title = u"WinFuture",
       .destination_url = GURL("https://winfuture.de/")},
      {.kind = CommandBarSuggestionKind::kLocalItem,
       .title = u"FPN Dichtstoffe",
       .item = CommandItem{.type = CommandItemType::kOpenTab,
                           .stable_id = "open-fpn",
                           .title = u"FPN Dichtstoffe",
                           .url = typed_destination},
       .destination_url = typed_destination},
  };

  suggestions = MergeCommandBarSuggestions(
      std::move(suggestions),
      CommandBarSuggestion{
          .kind = CommandBarSuggestionKind::kInputFallback,
          .title = u"fpn-dichtstoffe.de",
          .destination_url = typed_destination,
      },
      5u);

  ASSERT_EQ(2u, suggestions.size());
  EXPECT_EQ(CommandBarSuggestionKind::kLocalItem, suggestions.front().kind);
  EXPECT_EQ("open-fpn", suggestions.front().item->stable_id);
  EXPECT_EQ(typed_destination, suggestions.front().destination_url);
}

TEST(CommandBarTypesTest, SearchFallbackRemainsLastWhenResultsAreCapped) {
  std::vector<CommandBarSuggestion> suggestions;
  for (int index = 0; index < 5; ++index) {
    suggestions.push_back({.kind = CommandBarSuggestionKind::kLocalItem,
                           .title = u"Local result"});
  }

  suggestions = MergeCommandBarSuggestions(
      std::move(suggestions),
      CommandBarSuggestion{
          .kind = CommandBarSuggestionKind::kInputFallback,
          .title = u"plain search terms",
          .secondary_text = u"Search",
      },
      5u);

  ASSERT_EQ(5u, suggestions.size());
  EXPECT_EQ(CommandBarSuggestionKind::kInputFallback, suggestions.back().kind);
}

}  // namespace ahoi
