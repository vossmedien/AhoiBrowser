// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_TAB_SWITCHER_TAB_SWITCHER_CONTROLLER_H_
#define AHOI_BROWSER_UI_TAB_SWITCHER_TAB_SWITCHER_CONTROLLER_H_

#include <map>
#include <memory>
#include <vector>

#include "ahoi/browser/ui/tab_switcher/tab_switcher_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/tabs/public/tab_interface.h"

class BrowserWindowInterface;

namespace views {
class BubbleDialogDelegate;
class Widget;
}  // namespace views

namespace ahoi {

class ModalOverlayController;

namespace tab_switcher {

class TabSwitcherView;

// Owns the switcher panel of one browser window. The panel lists the open
// tabs of the window's active Workspace (never another Workspace's or
// window's) and acts on them through their TabHandles, so a tab closed while
// the panel is open is skipped instead of dereferenced.
class TabSwitcherController final {
 public:
  TabSwitcherController(BrowserWindowInterface* browser,
                        ModalOverlayController* modal_overlay_controller);
  TabSwitcherController(const TabSwitcherController&) = delete;
  TabSwitcherController& operator=(const TabSwitcherController&) = delete;
  ~TabSwitcherController();

  // Opens the panel, or moves to the next tile while it is open. False when
  // the window has no tab to show or another panel is open.
  bool Toggle();

 private:
  bool Show();
  // Collects the tabs again and replaces the tiles.
  void Rebuild(size_t focus, bool initial);
  void Activate(size_t index);
  void CloseEntry(size_t index);
  void RequestClose();
  void CloseBubbleNow();
  void OnBubbleClosed();

  const raw_ptr<BrowserWindowInterface> browser_;
  const raw_ptr<ModalOverlayController> modal_overlay_controller_;
  // Model ids of the last collection.
  std::map<int, tabs::TabHandle> tabs_;
  std::vector<Entry> entries_;
  raw_ptr<TabSwitcherView> view_ = nullptr;
  std::unique_ptr<views::BubbleDialogDelegate> bubble_delegate_;
  std::unique_ptr<views::Widget> bubble_widget_;
  base::WeakPtrFactory<TabSwitcherController> weak_ptr_factory_{this};
};

}  // namespace tab_switcher
}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_TAB_SWITCHER_TAB_SWITCHER_CONTROLLER_H_
