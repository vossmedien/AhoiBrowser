// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_TYPES_H_
#define AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_TYPES_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/navigation/command_service.h"
#include "ui/base/models/image_model.h"

namespace ahoi {

// Cmd+L keeps navigation in the selected tab. Cmd+T does not create a blank
// tab up front; it commits into a new foreground tab only after the user
// accepts a command-bar result.
enum class CommandBarDisposition {
  kCurrentTab = 0,
  kNewForegroundTab = 1,
};

enum class CommandBarSuggestionKind {
  kLocalItem = 0,
  kInputFallback = 1,
};

// Complete, presentation-ready suggestion. The command bar never asks an
// autocomplete provider for network suggestions. Local items come only from
// CommandService; the fallback is derived synchronously from the entered URL
// or the profile's configured default search provider.
struct CommandBarSuggestion {
  CommandBarSuggestionKind kind = CommandBarSuggestionKind::kLocalItem;
  std::u16string title;
  std::u16string secondary_text;
  ui::ImageModel icon;
  std::optional<CommandItem> item;
  // Presentation state for a live open-tab result. This is deliberately
  // independent from the command bar's keyboard selection: the current tab
  // stays identifiable while the user hovers or moves through other results.
  bool is_active_tab = false;
  // Canonical destination used only for presentation-level de-duplication.
  // In particular, a typed URL fallback must not be shown next to a richer
  // open-tab, saved-page or history result for that same URL.
  std::optional<GURL> destination_url;

  bool operator==(const CommandBarSuggestion&) const = default;
};

// Preserves ranking order and keeps the first, richest suggestion for every
// canonical URL. Suggestions without a destination (commands, folders and
// search fallbacks) remain untouched.
std::vector<CommandBarSuggestion> DeduplicateSuggestionsByDestination(
    std::vector<CommandBarSuggestion> suggestions);

// Combines ranked local results with the action derived from the literal
// input. A syntactically complete URL is always the default action: an exact
// local destination may represent it (and can activate an existing tab), but
// unrelated fuzzy history matches never precede it. Search fallbacks remain
// the final action so local results keep their normal ranking.
std::vector<CommandBarSuggestion> MergeCommandBarSuggestions(
    std::vector<CommandBarSuggestion> local_suggestions,
    std::optional<CommandBarSuggestion> input_fallback,
    size_t max_suggestions);

}  // namespace ahoi

#endif  // AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_TYPES_H_
