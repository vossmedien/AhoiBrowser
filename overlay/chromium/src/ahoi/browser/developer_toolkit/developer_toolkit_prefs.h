// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_PREFS_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_PREFS_H_

class PrefService;
class PrefRegistrySimple;

namespace ahoi::developer_toolkit_prefs {

inline constexpr char kToolkitEnabled[] = "ahoi.developer_toolkit.enabled";

inline constexpr char kShowCookieButton[] =
    "ahoi.developer_toolbar.show_cookie_button";
inline constexpr char kShowCacheButton[] =
    "ahoi.developer_toolbar.show_cache_button";
inline constexpr char kShowToolkitButton[] =
    "ahoi.developer_toolbar.show_toolkit_button";

struct ToolbarVisibility {
  bool cookie = false;
  bool cache = false;
  bool toolkit = false;

  bool any_visible() const { return cookie || cache || toolkit; }
  bool operator==(const ToolbarVisibility&) const = default;
};

void RegisterProfilePrefs(PrefRegistrySimple* registry);
ToolbarVisibility GetToolbarVisibility(const PrefService& prefs);
bool IsToolkitEnabled(const PrefService& prefs);

// Materializes the pre-master-switch activation signal once a real profile is
// opened, so runtime chrome and chrome://settings expose the same state.
void MigrateLegacyActivation(PrefService* prefs);

// Explicit one-time activation used by Settings or a dedicated command. It
// enables the compact main entry when no toolbar choices were configured.
bool ActivateToolkit(PrefService& prefs);
// Enabling through the master switch restores the compact toolkit entry when
// no developer toolbar action is configured, keeping activation recoverable.
void SetToolkitEnabled(PrefService& prefs, bool enabled);

// Toolbar actions may all be hidden; the master switch remains reachable from
// Settings/commands and is intentionally distinct from chrome customization.
bool SetToolbarVisibility(PrefService& prefs, ToolbarVisibility visibility);

}  // namespace ahoi::developer_toolkit_prefs

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_PREFS_H_
