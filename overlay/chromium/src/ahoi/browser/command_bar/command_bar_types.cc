// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/command_bar_types.h"

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

}  // namespace ahoi
