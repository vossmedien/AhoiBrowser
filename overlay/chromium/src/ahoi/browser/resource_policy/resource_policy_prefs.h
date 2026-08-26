// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_PREFS_H_
#define AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_PREFS_H_

class PrefRegistrySimple;

namespace ahoi::resource_policy {

// Changes only Chromium's registered default values. A user-set or managed
// pref retains normal PrefService precedence.
void ApplyLocalStateDefaults(PrefRegistrySimple* registry);

}  // namespace ahoi::resource_policy

#endif  // AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_PREFS_H_
