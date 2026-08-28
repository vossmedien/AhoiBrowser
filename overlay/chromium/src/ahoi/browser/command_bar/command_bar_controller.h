// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_CONTROLLER_H_
#define AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_CONTROLLER_H_

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/command_bar/command_bar_types.h"
#include "base/containers/lru_cache.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "url/gurl.h"

class Browser;
class TabStripModel;

namespace favicon {
class FaviconService;
}

namespace favicon_base {
struct FaviconImageResult;
}

namespace history {
class HistoryService;
class QueryResults;
}  // namespace history

namespace views {
class BubbleDialogDelegate;
class View;
class Widget;
}  // namespace views

namespace ahoi {

class CommandBarView;
class CommandExecutionAdapter;
class CommandService;
class ModalOverlayController;

// Per-BrowserView native command-bar coordinator. Regular profiles use their
// keyed CommandService. OTR and guest windows get an empty, process-local
// CommandService so URL/default-search parsing remains available without
// exposing regular-profile tabs, history, workspaces or saved pages.
class CommandBarController : public TabStripModelObserver,
                             public CommandServiceObserver {
 public:
  CommandBarController(Browser* browser,
                       ModalOverlayController* modal_overlay_controller,
                       views::View* sidebar_host);
  CommandBarController(const CommandBarController&) = delete;
  CommandBarController& operator=(const CommandBarController&) = delete;
  ~CommandBarController();

  // Returns false when the host window/profile cannot safely support the
  // command bar, allowing Chromium's original accelerator behavior to run.
  bool Show(CommandBarDisposition disposition);

 private:
  std::vector<CommandBarSuggestion> GetSuggestions(std::u16string_view input);
  bool ExecuteSuggestion(const CommandBarSuggestion& suggestion,
                         std::u16string_view original_input);
  std::u16string GetInitialQuery(CommandBarDisposition disposition) const;
  std::u16string GetPlaceholder() const;
  void PublishBrowserCommands();
  void RefreshHistoryItems();
  void OnHistoryQueryCompleted(history::QueryResults results);
  ui::ImageModel GetOrRequestFavicon(const GURL& page_url);
  void OnFaviconAvailable(const GURL& page_url,
                          const favicon_base::FaviconImageResult& result);
  void RequestClose();
  void CloseBubbleNow();
  void OnBubbleClosed();
  void OnViewDestroyed();

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // CommandServiceObserver:
  void OnCommandIndexChanged(CommandItemType type) override;

  void ScheduleSuggestionRefresh();

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<TabStripModel> tab_strip_model_ = nullptr;
  raw_ptr<ModalOverlayController> modal_overlay_controller_ = nullptr;
  raw_ptr<CommandService> command_service_ = nullptr;
  std::unique_ptr<CommandService> ephemeral_command_service_;
  std::unique_ptr<CommandExecutionAdapter> execution_adapter_;
  raw_ptr<history::HistoryService> history_service_ = nullptr;
  raw_ptr<favicon::FaviconService> favicon_service_ = nullptr;
  // Command-bar controllers can live for the whole browser-window lifetime.
  // Keep favicon memory bounded even after searching a very large history.
  base::LRUCache<GURL, ui::ImageModel> favicon_cache_{128u};
  std::set<GURL> requested_favicon_urls_;
  bool history_query_in_flight_ = false;
  base::TimeTicks last_history_refresh_;
  base::CancelableTaskTracker history_task_tracker_;
  base::CancelableTaskTracker favicon_task_tracker_;
  // The client-owned Widget must be destroyed before its delegate. Declaration
  // order supplies that fallback, while the explicit close path does the same.
  std::unique_ptr<views::BubbleDialogDelegate> bubble_delegate_;
  std::unique_ptr<views::Widget> bubble_widget_;
  raw_ptr<CommandBarView> view_ = nullptr;
  bool active_tab_refresh_pending_ = false;
  base::WeakPtrFactory<CommandBarController> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_CONTROLLER_H_
