// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_prefs.h"

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "base/values.h"
#include "components/pref_registry/pref_registry_syncable.h"

namespace ahoi::developer_profile_prefs {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  // The schema and origin map live in one dictionary so adding fields remains
  // atomic from PrefService's point of view. A missing schema is treated as an
  // empty store by the runtime and is initialized on the first successful set.
  registry->RegisterDictionaryPref(kDeveloperProfilesPref, base::DictValue());
}

}  // namespace ahoi::developer_profile_prefs
