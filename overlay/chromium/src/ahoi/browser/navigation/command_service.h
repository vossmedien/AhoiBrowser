// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_COMMAND_SERVICE_H_
#define AHOI_BROWSER_NAVIGATION_COMMAND_SERVICE_H_

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "components/keyed_service/core/keyed_service.h"
#include "url/gurl.h"

class AutocompleteSchemeClassifier;

namespace ahoi {

enum class CommandItemType {
  kOpenTab = 0,
  kSavedPage = 1,
  kFolder = 2,
  kWorkspace = 3,
  kHistory = 4,
  kBrowserCommand = 5,
};

struct CommandItem {
  CommandItemType type = CommandItemType::kOpenTab;
  std::string stable_id;
  std::u16string title;
  std::u16string secondary_text;
  std::vector<std::u16string> keywords;
  std::optional<GURL> url;
  int priority = 0;
  base::Time last_used;

  bool operator==(const CommandItem&) const = default;
};

struct RankedCommand {
  CommandItem item;
  int score = 0;

  bool operator==(const RankedCommand&) const = default;
};

// Per-surface query policy. The command bar retains its compact, destination-
// deduplicated behavior through the legacy overload below; native collections
// such as the sidebar can preserve multiple open tabs with the same URL while
// excluding unrelated history and browser-command sources.
struct CommandQueryOptions {
  std::set<CommandItemType> allowed_types;
  bool deduplicate_urls = true;
  size_t max_results = 0;

  bool operator==(const CommandQueryOptions&) const = default;
};

enum class CommandInputKind {
  kEmpty = 0,
  // A local query which may become a default-engine search if the user does
  // not choose a local result.
  kLocalOrSearch = 1,
  kUrl = 2,
  kSearchShortcut = 3,
  // Explicitly scoped local queries (`>`, `@tabs`, `@history`, `@tree`) must
  // never fall through to the configured search engine.
  kLocalOnly = 4,
};

struct ParsedCommandInput {
  CommandInputKind kind = CommandInputKind::kEmpty;
  std::u16string text;
  std::u16string search_shortcut;
  GURL url;

  bool operator==(const ParsedCommandInput&) const = default;
};

class CommandServiceObserver : public base::CheckedObserver {
 public:
  virtual void OnCommandIndexChanged(CommandItemType type) = 0;

 protected:
  ~CommandServiceObserver() override = default;
};

// Network-free index and parser intended to be owned by one regular Profile's
// keyed-service factory. Browser/history/tree adapters replace their own source
// snapshots; querying never invokes a remote suggestion endpoint.
class CommandService : public KeyedService {
 public:
  CommandService();
  CommandService(const CommandService&) = delete;
  CommandService& operator=(const CommandService&) = delete;
  ~CommandService() override;

  void AddObserver(CommandServiceObserver* observer);
  void RemoveObserver(CommandServiceObserver* observer);

  // Replaces one source atomically. Invalid items or duplicate stable IDs
  // reject the complete snapshot and leave the old index intact.
  [[nodiscard]] bool ReplaceItems(CommandItemType type,
                                  std::vector<CommandItem> items);
  void ClearItems(CommandItemType type);

  std::vector<RankedCommand> Query(std::u16string_view input,
                                   size_t max_results) const;
  std::vector<RankedCommand> Query(
      std::u16string_view input,
      const CommandQueryOptions& options) const;
  bool IsExplicitLocalQuery(std::u16string_view input) const;
  // The caller supplies Chromium's profile-aware scheme classifier for this
  // synchronous call. The service neither owns nor retains Profile state.
  ParsedCommandInput ParseInput(
      std::u16string_view input,
      const AutocompleteSchemeClassifier& scheme_classifier) const;

  [[nodiscard]] bool RegisterSearchShortcut(std::u16string shortcut);
  bool UnregisterSearchShortcut(std::u16string_view shortcut);

 private:
  struct IndexedItem {
    CommandItem item;
    std::u16string normalized_title;
    std::u16string search_blob;
  };

  static std::u16string Normalize(std::u16string_view text);
  static int ScoreText(std::u16string_view query, std::u16string_view text);
  static int ScoreItem(std::u16string_view query, const IndexedItem& item);

  std::map<CommandItemType, std::vector<IndexedItem>> index_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::set<std::u16string> search_shortcuts_
      GUARDED_BY_CONTEXT(sequence_checker_);
  base::ObserverList<CommandServiceObserver> observers_
      GUARDED_BY_CONTEXT(sequence_checker_);
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_NAVIGATION_COMMAND_SERVICE_H_
