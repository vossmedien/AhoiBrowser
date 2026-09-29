// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_KEYBOARD_SHORTCUT_REGISTRATION_H_
#define AHOI_BROWSER_NAVIGATION_KEYBOARD_SHORTCUT_REGISTRATION_H_

#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/navigation/keyboard_shortcuts.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/base/accelerators/accelerator.h"

class PrefService;

namespace ahoi::shortcuts {

// Keeps one window's accelerator registrations in line with the catalog and
// the user's bindings. Only rebindable commands are registered here; fixed
// ones keep their own registration. A changed binding takes effect at once.
class ShortcutRegistration {
 public:
  // `apply(accelerator, true)` registers, `apply(accelerator, false)`
  // unregisters. It is called for changes only.
  using Apply =
      base::RepeatingCallback<void(const ui::Accelerator&, bool register_it)>;

  ShortcutRegistration(PrefService* prefs, Apply apply);
  ShortcutRegistration(const ShortcutRegistration&) = delete;
  ShortcutRegistration& operator=(const ShortcutRegistration&) = delete;
  // Registrations are left to their owner's teardown.
  ~ShortcutRegistration();

  // Runs after the bindings changed and the registrations followed, for
  // state outside the window (main-menu key equivalents, the system-wide
  // Quick Window hotkey). Not run for the initial registration.
  void SetChangedCallback(base::RepeatingClosure changed);

  // The rebindable command `accelerator` triggers now, or nullopt.
  std::optional<std::string> CommandFor(
      const ui::Accelerator& accelerator) const;
  const Overrides& overrides() const { return overrides_; }

 private:
  void Refresh();

  raw_ptr<PrefService> prefs_;
  Apply apply_;
  base::RepeatingClosure changed_;
  PrefChangeRegistrar registrar_;
  Overrides overrides_;
  std::vector<ui::Accelerator> registered_;
};

}  // namespace ahoi::shortcuts

#endif  // AHOI_BROWSER_NAVIGATION_KEYBOARD_SHORTCUT_REGISTRATION_H_
