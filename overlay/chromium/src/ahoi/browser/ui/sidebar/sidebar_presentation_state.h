// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_PRESENTATION_STATE_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_PRESENTATION_STATE_H_

#include <optional>

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace ahoi::sidebar {

// The presentation is deliberately independent from the tab-tree model. It
// describes how the one native sidebar surface is placed by BrowserView.
enum class SidebarPresentationMode {
  kDocked = 0,
  kFloating = 1,
  kHidden = 2,
};

struct SidebarLayoutPolicy {
  bool visible = true;
  bool reserve_viewport = true;
  bool overlays_web_contents = false;
};

inline constexpr char kSidebarPresentationModePref[] =
    "ahoi.sidebar.presentation_mode";
inline constexpr char kSidebarPresentationModeBeforeHiddenPref[] =
    "ahoi.sidebar.presentation_mode_before_hidden";
inline constexpr char kSidebarMiniPlayerExpandedPref[] =
    "ahoi.sidebar.mini_player_expanded";

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

SidebarPresentationMode GetPresentationMode(const PrefService& prefs);

// Stores the mode and, when hiding the sidebar, remembers the previous
// visible mode so that the keyboard restore action can return to the exact
// docked/floating state the user had selected.
bool SetPresentationMode(PrefService* prefs, SidebarPresentationMode mode);

SidebarPresentationMode GetVisibleModeBeforeHidden(const PrefService& prefs);

bool IsMiniPlayerExpanded(const PrefService& prefs);
bool SetMiniPlayerExpanded(PrefService* prefs, bool expanded);

bool IsVisible(SidebarPresentationMode mode);
bool IsFloating(SidebarPresentationMode mode);
SidebarLayoutPolicy GetLayoutPolicy(SidebarPresentationMode mode);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_PRESENTATION_STATE_H_
