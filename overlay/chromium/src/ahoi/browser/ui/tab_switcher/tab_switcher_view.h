// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_TAB_SWITCHER_TAB_SWITCHER_VIEW_H_
#define AHOI_BROWSER_UI_TAB_SWITCHER_TAB_SWITCHER_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"
#include "ahoi/browser/ui/appearance/native_panel_material.h"
#include "ahoi/browser/ui/tab_switcher/tab_switcher_model.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/models/image_model.h"
#include "ui/views/view.h"

class PrefService;

namespace tabs {
class TabInterface;
}

namespace views {
class ScrollView;
}

namespace ahoi::tab_switcher {

class TabSwitcherTile;

inline constexpr int kPanelWidth = 720;

// "Tabs wechseln" in the browser language.
std::u16string PanelTitle();

// What a tile shows besides the model entry. `preview_tab` is the pane whose
// thumbnail is shown; it is only read while the panel builds its tiles.
struct EntryVisual {
  ui::ImageModel favicon;
  raw_ptr<tabs::TabInterface> preview_tab = nullptr;
};

// The switcher panel: header, one tile grid per section and the key hints.
// Arrow keys move, Return or a click opens, W closes the entry's tabs, ⌃T
// moves to the next tile; Escape is the bubble's cancel.
class TabSwitcherView final : public views::View {
 public:
  struct Callbacks {
    base::RepeatingCallback<void(size_t)> activate;
    base::RepeatingCallback<void(size_t)> close;
  };

  TabSwitcherView(std::u16string workspace_name,
                  PrefService* prefs,
                  Callbacks callbacks);
  TabSwitcherView(const TabSwitcherView&) = delete;
  TabSwitcherView& operator=(const TabSwitcherView&) = delete;
  ~TabSwitcherView() override;

  // Replaces the grid and focuses `focus`.
  void SetEntries(std::vector<Entry> entries,
                  std::vector<EntryVisual> visuals,
                  size_t focus);
  void FocusCurrent();
  // ⌃T while the panel is open: the next tile, wrapping.
  void FocusNext();
  void ReapplyAppearance();

  size_t focus_for_testing() const { return focus_; }
  bool HandleKeyForTesting(const ui::KeyEvent& event) {
    return HandleKey(event);
  }

 private:
  bool HandleKey(const ui::KeyEvent& event);
  void SetFocusIndex(size_t index);
  void OnAppearanceChanged(const appearance::GlassPolicy& policy);

  const Callbacks callbacks_;
  std::vector<Entry> entries_;
  std::vector<raw_ptr<TabSwitcherTile>> tiles_;
  size_t focus_ = 0;
  raw_ptr<views::View> grid_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  appearance::NativePanelMaterial panel_material_{
      appearance::SurfaceRole::kCommandBar};
  std::unique_ptr<appearance::AppearanceRuntimeSignalSource>
      appearance_signal_source_;
  base::WeakPtrFactory<TabSwitcherView> weak_ptr_factory_{this};
};

}  // namespace ahoi::tab_switcher

#endif  // AHOI_BROWSER_UI_TAB_SWITCHER_TAB_SWITCHER_VIEW_H_
