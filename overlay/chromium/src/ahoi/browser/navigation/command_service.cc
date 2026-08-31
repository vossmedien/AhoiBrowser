// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/command_service.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/i18n/case_conversion.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/timer/elapsed_timer.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_scheme_classifier.h"

namespace ahoi {

namespace {

enum class QueryScope {
  kAll,
  kBrowserCommands,
  kTabs,
  kHistory,
  kTree,
};

struct ScopedQuery {
  QueryScope scope = QueryScope::kAll;
  std::u16string text;
  bool explicit_local = false;
};

ScopedQuery ParseScopedQuery(std::u16string normalized) {
  ScopedQuery parsed{.text = std::move(normalized)};
  if (parsed.text.starts_with(u">")) {
    parsed.scope = QueryScope::kBrowserCommands;
    parsed.explicit_local = true;
    parsed.text.erase(0, 1);
    base::TrimWhitespace(parsed.text, base::TrimPositions::TRIM_ALL,
                         &parsed.text);
    return parsed;
  }

  struct Prefix {
    std::u16string_view text;
    QueryScope scope;
  };
  constexpr Prefix kPrefixes[] = {
      {u"@tabs", QueryScope::kTabs},
      {u"@history", QueryScope::kHistory},
      {u"@tree", QueryScope::kTree},
  };
  for (const Prefix& prefix : kPrefixes) {
    if (parsed.text != prefix.text &&
        !parsed.text.starts_with(std::u16string(prefix.text) + u" ")) {
      continue;
    }
    parsed.scope = prefix.scope;
    parsed.explicit_local = true;
    parsed.text.erase(0, prefix.text.size());
    base::TrimWhitespace(parsed.text, base::TrimPositions::TRIM_ALL,
                         &parsed.text);
    return parsed;
  }
  return parsed;
}

bool TypeMatchesScope(CommandItemType type, QueryScope scope) {
  switch (scope) {
    case QueryScope::kAll:
      return true;
    case QueryScope::kBrowserCommands:
      return type == CommandItemType::kBrowserCommand;
    case QueryScope::kTabs:
      return type == CommandItemType::kOpenTab ||
             type == CommandItemType::kDeviceTab;
    case QueryScope::kHistory:
      return type == CommandItemType::kHistory;
    case QueryScope::kTree:
      return type == CommandItemType::kSavedPage ||
             type == CommandItemType::kFolder ||
             type == CommandItemType::kWorkspace;
  }
  return false;
}

bool IsItemShapeValid(const CommandItem& item) {
  const bool has_valid_url =
      item.url.has_value() && item.url->is_valid() && !item.url->is_empty() &&
      !item.url->has_username() && !item.url->has_password();
  switch (item.type) {
    case CommandItemType::kOpenTab:
    case CommandItemType::kSavedPage:
    case CommandItemType::kDeviceTab:
    case CommandItemType::kHistory:
      return has_valid_url;
    case CommandItemType::kFolder:
    case CommandItemType::kWorkspace:
    case CommandItemType::kBrowserCommand:
      return !item.url.has_value();
  }
  return false;
}

}  // namespace

CommandService::CommandService() {
  CHECK(RegisterSearchShortcut(u"g"));
}

CommandService::~CommandService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void CommandService::AddObserver(CommandServiceObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void CommandService::RemoveObserver(CommandServiceObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

bool CommandService::ReplaceItems(CommandItemType type,
                                  std::vector<CommandItem> items) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::set<std::string> stable_ids;
  std::vector<IndexedItem> replacement;
  replacement.reserve(items.size());

  for (auto& item : items) {
    if (item.type != type || item.stable_id.empty() || item.title.empty() ||
        !IsItemShapeValid(item) || !stable_ids.insert(item.stable_id).second) {
      return false;
    }

    IndexedItem indexed;
    indexed.normalized_title = Normalize(item.title);
    if (indexed.normalized_title.empty()) {
      return false;
    }
    std::vector<std::u16string> searchable;
    searchable.reserve(item.keywords.size() + 2u);
    searchable.push_back(indexed.normalized_title);
    searchable.push_back(Normalize(item.secondary_text));
    for (const auto& keyword : item.keywords) {
      searchable.push_back(Normalize(keyword));
    }
    std::erase(searchable, std::u16string());
    indexed.search_blob = base::JoinString(searchable, u" ");
    indexed.item = std::move(item);
    replacement.push_back(std::move(indexed));
  }

  index_.insert_or_assign(type, std::move(replacement));
  for (auto& observer : observers_) {
    observer.OnCommandIndexChanged(type);
  }
  return true;
}

void CommandService::ClearItems(CommandItemType type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (index_.erase(type) == 0u) {
    return;
  }
  for (auto& observer : observers_) {
    observer.OnCommandIndexChanged(type);
  }
}

std::vector<RankedCommand> CommandService::Query(std::u16string_view input,
                                                 size_t max_results) const {
  // Preserve the command bar's pre-device-tabs contract. Remote items need a
  // surface that can resolve their opaque identity against a current sync
  // snapshot, which the command execution adapter deliberately cannot do.
  return Query(input, CommandQueryOptions{
                          .allowed_types = {CommandItemType::kOpenTab,
                                            CommandItemType::kSavedPage,
                                            CommandItemType::kFolder,
                                            CommandItemType::kWorkspace,
                                            CommandItemType::kHistory,
                                            CommandItemType::kBrowserCommand},
                          .max_results = max_results,
                      });
}

std::vector<RankedCommand> CommandService::Query(
    std::u16string_view input,
    const CommandQueryOptions& options) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (options.max_results == 0u) {
    return {};
  }
  base::ElapsedTimer query_timer;

  struct ScoredItem {
    raw_ptr<const IndexedItem> indexed = nullptr;
    int score = 0;
  };
  std::vector<ScoredItem> scored_items;
  const ScopedQuery query = ParseScopedQuery(Normalize(input));
  for (const auto& entry : index_) {
    if (!TypeMatchesScope(entry.first, query.scope) ||
        (!options.allowed_types.empty() &&
         !options.allowed_types.contains(entry.first))) {
      continue;
    }
    for (const auto& indexed : entry.second) {
      const int score = ScoreItem(query.text, indexed);
      if (score >= 0) {
        scored_items.push_back({&indexed, score});
      }
    }
  }

  const auto is_better = [](const ScoredItem& lhs, const ScoredItem& rhs) {
    if (lhs.score != rhs.score) {
      return lhs.score > rhs.score;
    }
    const CommandItem& lhs_item = lhs.indexed->item;
    const CommandItem& rhs_item = rhs.indexed->item;
    if (lhs_item.last_used != rhs_item.last_used) {
      return lhs_item.last_used > rhs_item.last_used;
    }
    if (lhs_item.priority != rhs_item.priority) {
      return lhs_item.priority > rhs_item.priority;
    }
    if (lhs_item.title != rhs_item.title) {
      return lhs_item.title < rhs_item.title;
    }
    if (lhs_item.stable_id != rhs_item.stable_id) {
      return lhs_item.stable_id < rhs_item.stable_id;
    }
    return lhs_item.type < rhs_item.type;
  };
  std::ranges::sort(scored_items, is_better);

  std::vector<RankedCommand> results;
  results.reserve(std::min(options.max_results, scored_items.size()));
  std::set<GURL> seen_urls;
  for (const ScoredItem& scored : scored_items) {
    const CommandItem& item = scored.indexed->item;
    // One destination should appear once even if it is simultaneously an open
    // tab, a saved page and a history entry. This also intentionally collapses
    // duplicate open tabs in the command bar without affecting the sidebar.
    if (options.deduplicate_urls && item.url.has_value() &&
        !seen_urls.insert(*item.url).second) {
      continue;
    }
    results.push_back({item, scored.score});
    if (results.size() == options.max_results) {
      break;
    }
  }
  // CMD-06: measure only the local synchronous ranking path. The query text,
  // result identities and URLs are deliberately never recorded.
  base::UmaHistogramMicrosecondsTimes("Ahoi.CommandBar.QueryLatency",
                                      query_timer.Elapsed());
  return results;
}

bool CommandService::IsExplicitLocalQuery(std::u16string_view input) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return ParseScopedQuery(Normalize(input)).explicit_local;
}

ParsedCommandInput CommandService::ParseInput(
    std::u16string_view input,
    const AutocompleteSchemeClassifier& scheme_classifier) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ParsedCommandInput parsed;
  const std::u16string_view trimmed =
      base::TrimWhitespace(input, base::TrimPositions::TRIM_ALL);
  if (trimmed.empty()) {
    return parsed;
  }

  const ScopedQuery scoped = ParseScopedQuery(Normalize(trimmed));
  if (scoped.explicit_local) {
    parsed.kind = CommandInputKind::kLocalOnly;
    parsed.text = scoped.text;
    return parsed;
  }

  const size_t delimiter = trimmed.find_first_of(base::kWhitespaceUTF16);
  if (delimiter != std::u16string_view::npos) {
    const std::u16string shortcut = Normalize(trimmed.substr(0, delimiter));
    const std::u16string_view terms = base::TrimWhitespace(
        trimmed.substr(delimiter), base::TrimPositions::TRIM_ALL);
    if (!terms.empty() && search_shortcuts_.contains(shortcut)) {
      parsed.kind = CommandInputKind::kSearchShortcut;
      parsed.text = std::u16string(terms);
      parsed.search_shortcut = shortcut;
      return parsed;
    }
  }

  // Reuse Chromium's local omnibox parser instead of maintaining a second URL
  // heuristic. The profile adapter supplies ChromeAutocompleteSchemeClassifier
  // so internal, registered and external protocols retain Chromium semantics.
  AutocompleteInput autocomplete_input(
      std::u16string(trimmed), metrics::OmniboxEventProto::OTHER,
      scheme_classifier, /*should_use_https_as_default_scheme=*/true);
  if (autocomplete_input.type() == metrics::OmniboxInputType::URL &&
      autocomplete_input.canonicalized_url().is_valid()) {
    parsed.kind = CommandInputKind::kUrl;
    parsed.text = std::u16string(trimmed);
    parsed.url = autocomplete_input.canonicalized_url();
    return parsed;
  }

  parsed.kind = CommandInputKind::kLocalOrSearch;
  parsed.text = std::u16string(trimmed);
  return parsed;
}

bool CommandService::RegisterSearchShortcut(std::u16string shortcut) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  shortcut = Normalize(shortcut);
  if (shortcut.empty() || shortcut.find(u' ') != std::u16string::npos) {
    return false;
  }
  return search_shortcuts_.insert(std::move(shortcut)).second;
}

bool CommandService::UnregisterSearchShortcut(std::u16string_view shortcut) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return search_shortcuts_.erase(Normalize(shortcut)) != 0u;
}

std::u16string CommandService::Normalize(std::u16string_view text) {
  std::vector<std::u16string> tokens =
      base::SplitString(base::i18n::FoldCase(text), base::kWhitespaceUTF16,
                        base::WhitespaceHandling::TRIM_WHITESPACE,
                        base::SplitResult::SPLIT_WANT_NONEMPTY);
  return base::JoinString(tokens, u" ");
}

int CommandService::ScoreText(std::u16string_view query,
                              std::u16string_view text) {
  if (query.empty()) {
    return 0;
  }
  if (text.empty()) {
    return -1;
  }
  if (text == query) {
    return 10000;
  }
  if (text.starts_with(query)) {
    return 8500 -
           static_cast<int>(std::min<size_t>(text.size() - query.size(), 500u));
  }
  const size_t substring = text.find(query);
  if (substring != std::u16string_view::npos) {
    return 7000 - static_cast<int>(std::min<size_t>(substring, 1000u));
  }

  size_t query_index = 0;
  size_t first_match = std::u16string_view::npos;
  size_t previous_match = 0;
  size_t gaps = 0;
  for (size_t text_index = 0;
       text_index < text.size() && query_index < query.size(); ++text_index) {
    if (text[text_index] != query[query_index]) {
      continue;
    }
    if (first_match == std::u16string_view::npos) {
      first_match = text_index;
    } else {
      gaps += text_index - previous_match - 1u;
    }
    previous_match = text_index;
    ++query_index;
  }
  if (query_index != query.size()) {
    return -1;
  }

  const size_t penalty = std::min<size_t>(first_match + gaps, 1500u);
  return 4000 - static_cast<int>(penalty);
}

int CommandService::ScoreItem(std::u16string_view query,
                              const IndexedItem& item) {
  const int priority = std::clamp(item.item.priority, -1000, 1000);
  if (query.empty()) {
    return priority;
  }

  const int title_score = ScoreText(query, item.normalized_title);
  const int blob_score = ScoreText(query, item.search_blob);
  const int best =
      std::max(title_score < 0 ? title_score : title_score + 250, blob_score);
  return best < 0 ? -1 : best + priority;
}

}  // namespace ahoi
