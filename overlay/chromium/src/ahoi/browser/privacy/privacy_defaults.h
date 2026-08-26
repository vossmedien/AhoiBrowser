// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_PRIVACY_PRIVACY_DEFAULTS_H_
#define AHOI_BROWSER_PRIVACY_PRIVACY_DEFAULTS_H_

class PrefRegistrySimple;

namespace ahoi::privacy {

// Overrides only registered default values. User and enterprise values retain
// normal PrefService precedence.
void ApplyProfileDefaults(PrefRegistrySimple* registry);
void ApplyLocalStateDefaults(PrefRegistrySimple* registry);

}  // namespace ahoi::privacy

#endif  // AHOI_BROWSER_PRIVACY_PRIVACY_DEFAULTS_H_
