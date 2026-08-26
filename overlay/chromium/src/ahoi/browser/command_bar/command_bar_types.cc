// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/command_bar_types.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <utility>

namespace ahoi {

std::vector<CommandBarSuggestion> DeduplicateSuggestionsByDestination(
    std::vector<CommandBarSuggestion> suggestions) {
  std::set<GURL> seen_destinations;
  std::erase_if(suggestions, [&seen_destinations](const auto& suggestion) {
    return suggestion.destination_url.has_value() &&
           !seen_destinations.insert(*suggestion.destination_url).second;
  });
  return suggestions;
}

std::vector<CommandBarSuggestion> MergeCommandBarSuggestions(
    std::vector<CommandBarSuggestion> local_suggestions,
    std::optional<CommandBarSuggestion> input_fallback,
    size_t max_suggestions) {
  if (max_suggestions == 0u) {
    return {};
  }

  const bool has_navigation_fallback =
      input_fallback.has_value() &&
      input_fallback->kind == CommandBarSuggestionKind::kInputFallback &&
      input_fallback->destination_url.has_value();
  const bool keep_search_fallback_last =
      input_fallback.has_value() && !has_navigation_fallback;

  if (has_navigation_fallback) {
    const GURL& typed_destination = *input_fallback->destination_url;
    const auto exact_local = std::find_if(
        local_suggestions.begin(), local_suggestions.end(),
        [&typed_destination](const CommandBarSuggestion& suggestion) {
          return suggestion.destination_url == typed_destination;
        });
    if (exact_local == local_suggestions.end()) {
      local_suggestions.insert(local_suggestions.begin(),
                               std::move(*input_fallback));
    } else if (exact_local != local_suggestions.begin()) {
      std::rotate(local_suggestions.begin(), exact_local,
                  std::next(exact_local));
    }
  } else if (input_fallback.has_value()) {
    local_suggestions.push_back(std::move(*input_fallback));
  }

  local_suggestions =
      DeduplicateSuggestionsByDestination(std::move(local_suggestions));
  if (local_suggestions.size() <= max_suggestions) {
    return local_suggestions;
  }
  if (keep_search_fallback_last) {
    CommandBarSuggestion fallback = std::move(local_suggestions.back());
    local_suggestions.resize(max_suggestions - 1u);
    local_suggestions.push_back(std::move(fallback));
  } else {
    local_suggestions.resize(max_suggestions);
  }
  return local_suggestions;
}

}  // namespace ahoi
