// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_PREFS_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_PREFS_H_

class PrefRegistrySimple;

namespace ahoi::developer_profile_prefs {

void RegisterProfilePrefs(PrefRegistrySimple* registry);

}  // namespace ahoi::developer_profile_prefs

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_PREFS_H_
