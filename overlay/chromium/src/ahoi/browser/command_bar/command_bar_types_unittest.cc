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

}  // namespace ahoi
