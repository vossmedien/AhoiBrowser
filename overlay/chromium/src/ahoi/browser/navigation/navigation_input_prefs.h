// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_NAVIGATION_INPUT_PREFS_H_
#define AHOI_BROWSER_NAVIGATION_NAVIGATION_INPUT_PREFS_H_

#include "ahoi/browser/navigation/cmd_scroll_tab_switcher.h"
#include "ahoi/browser/navigation/workspace_swipe_tracker.h"

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace ahoi::navigation_input_prefs {

inline constexpr char kWorkspaceSwipeEnabled[] =
    "ahoi.navigation.workspace_swipe.enabled";
inline constexpr char kWorkspaceSwipeReverseDirection[] =
    "ahoi.navigation.workspace_swipe.reverse_direction";
inline constexpr char kWorkspaceSwipeThreshold[] =
    "ahoi.navigation.workspace_swipe.threshold";
inline constexpr char kWorkspaceSwipeAxisBias[] =
    "ahoi.navigation.workspace_swipe.axis_bias";
inline constexpr char kWorkspaceSwipeRejectVerticalDistance[] =
    "ahoi.navigation.workspace_swipe.reject_vertical_distance";
inline constexpr char kCmdScrollEnabled[] =
    "ahoi.navigation.cmd_scroll.enabled";
inline constexpr char kCmdScrollThreshold[] =
    "ahoi.navigation.cmd_scroll.threshold";
inline constexpr char kCmdScrollMinimumIntervalMs[] =
    "ahoi.navigation.cmd_scroll.minimum_interval_ms";
inline constexpr char kMiddleClickAutoscrollEnabled[] =
    "ahoi.navigation.middle_click_autoscroll.enabled";

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

WorkspaceSwipeSettings ReadWorkspaceSwipeSettings(const PrefService& prefs);
CmdScrollTabSettings ReadCmdScrollTabSettings(const PrefService& prefs);
bool IsMiddleClickAutoscrollEnabled(const PrefService& prefs);

}  // namespace ahoi::navigation_input_prefs

#endif  // AHOI_BROWSER_NAVIGATION_NAVIGATION_INPUT_PREFS_H_
