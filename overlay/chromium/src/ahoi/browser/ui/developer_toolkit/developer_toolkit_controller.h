// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_CONTROLLER_H_
#define AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_CONTROLLER_H_

#include <memory>

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_types.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"

class Browser;

namespace content {
class WebContents;
}

namespace views {
class BubbleDialogDelegate;
class View;
class Widget;
}  // namespace views

namespace ahoi {

class DeveloperActionExecutor;
class DeveloperCacheStatusView;
class DeveloperCookieManagerView;
class DeveloperProfileEditorView;
struct DeveloperProfile;

enum class DeveloperToolbarSurface {
  kToolkit,
  kCookieManager,
  kCacheClear,
};

// Per-window native coordinator. The production executor is created only on
// first use; merely showing normal browser chrome adds no observers, timers,
// renderer hooks or background work.
class DeveloperToolkitController {
 public:
  explicit DeveloperToolkitController(Browser* browser);
  DeveloperToolkitController(const DeveloperToolkitController&) = delete;
  DeveloperToolkitController& operator=(const DeveloperToolkitController&) =
      delete;
  ~DeveloperToolkitController();

  // Location-bar buttons keep using BrowserView's existing generic toolkit
  // route. This property tells the controller which dedicated surface belongs
  // to an anchor without widening BrowserView's API.
  static void ConfigureToolbarAnchor(views::View* anchor_view,
                                     DeveloperToolbarSurface surface);

  bool Show(views::View* anchor_view);
  bool IsSurfaceShowing(DeveloperToolbarSurface surface) const;
  bool CanExecute() const;
  DeveloperActionResult Execute(DeveloperAction action);
  bool ClearBrowsingData(BrowsingDataClearOptions options,
                         BrowsingDataClearCallback callback);

 private:
  content::WebContents* GetActiveWebContents() const;
  content::WebContents* GetToolkitWebContents() const;
  void ActivateToolkitWebContents();
  developer_toolkit_prefs::ToolbarVisibility GetToolbarVisibility() const;
  bool SetToolbarVisibility(
      developer_toolkit_prefs::ToolbarVisibility visibility);
  DeveloperActionExecutor* GetOrCreateExecutor();
  DeveloperActivationState GetActivationState();
  void OpenDevTools();
  void OpenPasswordManager();
  void OpenCookieManager(views::View* anchor_view);
  void OpenProfileEditor(views::View* anchor_view);
  bool ShowCookieManager(views::View* anchor_view);
  bool ShowCacheClear(views::View* anchor_view);
  bool ShowProfileEditor(views::View* anchor_view);
  bool SaveProfile(const DeveloperProfile& profile);
  bool RemoveProfile();
  void CloseProfileEditor();
  void OnCacheClearFinished(BrowsingDataClearResult result);
  void OnBubbleClosed();
  void OnCookieManagerClosed();
  void OnCacheStatusClosed();
  void OnProfileEditorClosed();

  raw_ptr<Browser> browser_ = nullptr;
  std::unique_ptr<DeveloperActionExecutor> executor_;
  base::WeakPtr<content::WebContents> bubble_contents_;
  std::unique_ptr<views::BubbleDialogDelegate> bubble_delegate_;
  std::unique_ptr<views::Widget> bubble_widget_;
  raw_ptr<DeveloperCookieManagerView> cookie_manager_view_ = nullptr;
  std::unique_ptr<views::BubbleDialogDelegate> cookie_manager_delegate_;
  std::unique_ptr<views::Widget> cookie_manager_widget_;
  raw_ptr<DeveloperCacheStatusView> cache_status_view_ = nullptr;
  std::unique_ptr<views::BubbleDialogDelegate> cache_status_delegate_;
  std::unique_ptr<views::Widget> cache_status_widget_;
  bool cache_clear_in_flight_ = false;
  raw_ptr<DeveloperProfileEditorView> profile_editor_view_ = nullptr;
  std::unique_ptr<views::BubbleDialogDelegate> profile_editor_delegate_;
  std::unique_ptr<views::Widget> profile_editor_widget_;
  base::WeakPtrFactory<DeveloperToolkitController> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_CONTROLLER_H_
