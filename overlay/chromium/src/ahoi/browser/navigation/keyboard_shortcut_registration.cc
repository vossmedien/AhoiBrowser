// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/keyboard_shortcut_registration.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "components/prefs/pref_service.h"

namespace ahoi::shortcuts {

ShortcutRegistration::ShortcutRegistration(PrefService* prefs, Apply apply)
    : prefs_(prefs), apply_(std::move(apply)) {
  if (prefs_ && prefs_->FindPreference(kShortcutBindingsPref)) {
    registrar_.Init(prefs_);
    registrar_.Add(kShortcutBindingsPref,
                   base::BindRepeating(&ShortcutRegistration::Refresh,
                                       base::Unretained(this)));
  }
  Refresh();
}

ShortcutRegistration::~ShortcutRegistration() = default;

std::optional<std::string> ShortcutRegistration::CommandFor(
    const ui::Accelerator& accelerator) const {
  std::optional<std::string> id = CommandForAccelerator(overrides_, accelerator);
  const ShortcutCommand* command = id ? FindCommand(*id) : nullptr;
  return command && command->rebindable ? id : std::nullopt;
}

void ShortcutRegistration::Refresh() {
  overrides_ = prefs_ ? ReadOverrides(*prefs_) : Overrides();
  std::vector<ui::Accelerator> wanted;
  for (const ShortcutCommand& command : Catalog()) {
    if (!command.rebindable) {
      continue;
    }
    for (const ui::Accelerator& accelerator :
         EffectiveAccelerators(overrides_, command.id)) {
      if (!std::ranges::contains(wanted, accelerator)) {
        wanted.push_back(accelerator);
      }
    }
  }
  for (const ui::Accelerator& accelerator : registered_) {
    if (!std::ranges::contains(wanted, accelerator)) {
      VLOG(1) << "Ahoi shortcut unregistered: " << Serialize(accelerator);
      apply_.Run(accelerator, false);
    }
  }
  for (const ui::Accelerator& accelerator : wanted) {
    if (!std::ranges::contains(registered_, accelerator)) {
      VLOG(1) << "Ahoi shortcut registered: " << Serialize(accelerator);
      apply_.Run(accelerator, true);
    }
  }
  registered_ = std::move(wanted);
}

}  // namespace ahoi::shortcuts
