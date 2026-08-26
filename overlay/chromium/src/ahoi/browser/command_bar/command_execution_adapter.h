// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_COMMAND_BAR_COMMAND_EXECUTION_ADAPTER_H_
#define AHOI_BROWSER_COMMAND_BAR_COMMAND_EXECUTION_ADAPTER_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ahoi/browser/command_bar/command_bar_types.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_types.h"
#include "base/memory/raw_ptr.h"
#include "url/gurl.h"

class AutocompleteSchemeClassifier;
class Browser;

namespace views {
class View;
}

namespace ahoi {

struct ResolvedCommandBarSearch {
  GURL url;
  std::u16string engine_name;
  std::string content_type;
  std::string post_data;

  bool operator==(const ResolvedCommandBarSearch&) const = default;
};

// Navigation metadata that the regular omnibox normally derives from the
// user's raw input. The command bar parses synchronously, so it has to carry
// these two bits across the execution boundary as well. In particular,
// explicit `http://` input must opt out of Chromium's HTTPS-first upgrade;
// otherwise a local/plain HTTP target can be shown as pending forever while
// the browser is actually trying TLS.
struct CommandBarNavigationHints {
  bool is_using_https_as_default_scheme = false;
  bool url_typed_with_http_scheme = false;

  bool operator==(const CommandBarNavigationHints&) const = default;
};

// Injectable boundary around TemplateURLService. The production implementation
// reads only the profile's local default-search configuration and performs no
// suggestion request.
class CommandBarSearchResolver {
 public:
  virtual ~CommandBarSearchResolver() = default;

  virtual std::u16string GetEngineName() const = 0;
  virtual std::optional<ResolvedCommandBarSearch> Resolve(
      std::u16string_view terms) const = 0;
};

// Browser mutation boundary. The production implementation uses Browser,
// TabStripModel, SessionBridge and Navigate(); tests inject a deterministic
// delegate without constructing a native browser window.
class CommandExecutionDelegate {
 public:
  virtual ~CommandExecutionDelegate() = default;

  virtual bool ActivateOpenTab(const CommandItem& item) = 0;
  virtual bool Navigate(const GURL& url,
                        std::string_view content_type,
                        std::string_view post_data,
                        CommandBarDisposition disposition,
                        bool is_search,
                        CommandBarNavigationHints navigation_hints = {}) = 0;
  virtual bool SwitchWorkspace(std::string_view stable_id) = 0;
  virtual bool CanRevealFolder(std::string_view stable_id) const = 0;
  virtual bool RevealFolder(std::string_view stable_id) = 0;
  virtual bool CanExecuteBrowserCommand(std::string_view stable_id) const = 0;
  virtual bool ExecuteBrowserCommand(std::string_view stable_id) = 0;
  virtual bool CanExecuteDeveloperAction(DeveloperAction action) const = 0;
  virtual bool ExecuteDeveloperAction(DeveloperAction action) = 0;
};

class CommandExecutionAdapter {
 public:
  static std::unique_ptr<CommandExecutionAdapter> CreateForBrowser(
      Browser* browser,
      CommandService* command_service,
      views::View* sidebar_host);

  CommandExecutionAdapter(
      CommandService* command_service,
      std::unique_ptr<AutocompleteSchemeClassifier> scheme_classifier,
      std::unique_ptr<CommandBarSearchResolver> search_resolver,
      std::unique_ptr<CommandExecutionDelegate> execution_delegate);
  CommandExecutionAdapter(const CommandExecutionAdapter&) = delete;
  CommandExecutionAdapter& operator=(const CommandExecutionAdapter&) = delete;
  ~CommandExecutionAdapter();

  std::u16string GetDefaultSearchEngineName() const;
  std::optional<CommandBarSuggestion> PreviewInput(
      std::u16string_view input) const;
  bool CanExecuteItem(const CommandItem& item) const;
  bool ExecuteInput(std::u16string_view input,
                    CommandBarDisposition disposition);
  bool ExecuteItem(const CommandItem& item, CommandBarDisposition disposition);

 private:
  raw_ptr<CommandService> command_service_ = nullptr;
  std::unique_ptr<AutocompleteSchemeClassifier> scheme_classifier_;
  std::unique_ptr<CommandBarSearchResolver> search_resolver_;
  std::unique_ptr<CommandExecutionDelegate> execution_delegate_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_COMMAND_BAR_COMMAND_EXECUTION_ADAPTER_H_
