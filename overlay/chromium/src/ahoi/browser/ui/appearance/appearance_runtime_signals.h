// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_RUNTIME_SIGNALS_H_
#define AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_RUNTIME_SIGNALS_H_

#include "ahoi/browser/ui/appearance/appearance_policy.h"
#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;

namespace ahoi::appearance {

// Signals that Chromium's OS settings provider can read directly. Battery and
// performance signals intentionally do not live here because their ownership
// varies by browser power/performance service; callers inject those through
// the setters below.
struct SystemAppearanceSignals {
  bool system_reduce_transparency = false;
  bool high_contrast = false;
  bool reduced_motion = false;
};

constexpr bool operator==(const SystemAppearanceSignals& lhs,
                          const SystemAppearanceSignals& rhs) {
  return lhs.system_reduce_transparency == rhs.system_reduce_transparency &&
         lhs.high_contrast == rhs.high_contrast &&
         lhs.reduced_motion == rhs.reduced_motion;
}

constexpr bool operator!=(const SystemAppearanceSignals& lhs,
                          const SystemAppearanceSignals& rhs) {
  return !(lhs == rhs);
}

// Browser-window-local bridge from Chromium's OsSettingsProvider to the pure
// GlassPolicy resolver. The default constructor subscribes to native settings
// changes; tests and embedders can inject a reader to keep platform state
// deterministic and to avoid owning a global observer subscription.
class AppearanceRuntimeSignalSource final {
 public:
  using ChangeCallback = AppearanceState::ChangeCallback;
  using SystemSignalReader =
      base::RepeatingCallback<SystemAppearanceSignals()>;

  explicit AppearanceRuntimeSignalSource(ChangeCallback changed);
  AppearanceRuntimeSignalSource(PrefService* prefs, ChangeCallback changed);
  AppearanceRuntimeSignalSource(ChangeCallback changed,
                                SystemSignalReader system_signal_reader);
  AppearanceRuntimeSignalSource(PrefService* prefs,
                                ChangeCallback changed,
                                SystemSignalReader system_signal_reader);
  AppearanceRuntimeSignalSource(const AppearanceRuntimeSignalSource&) = delete;
  AppearanceRuntimeSignalSource& operator=(
      const AppearanceRuntimeSignalSource&) = delete;
  ~AppearanceRuntimeSignalSource();

  // Reads the current system settings. This is called automatically at
  // construction and after OsSettingsProvider notifications, but is public so
  // an embedder can refresh after a deferred native-theme initialization.
  void RefreshSystemSignals();

  // Battery and performance are explicit inputs because Ahoi can receive them
  // from a browser power service without coupling this package to that
  // service's lifecycle.
  void SetBatterySaver(bool battery_saver);
  void SetPerformancePressure(PerformancePressure pressure);

  // Product/platform policy knobs that remain separate from native settings.
  void SetEnabled(bool enabled);
  void SetPlatformSupportsGlass(bool supports_glass);

  const GlassPolicy& policy() const { return policy_; }
  GlassMode mode() const { return AppearanceResolver::ResolveMode(policy_); }

  // Uses Chromium's stable cross-platform abstraction. On macOS this maps to
  // NSWorkspace.accessibilityDisplayShouldReduceTransparency and
  // accessibilityDisplayShouldIncreaseContrast through OsSettingsProviderMac.
  static SystemAppearanceSignals ReadCurrentSystemSignals();

 private:
  static GlassPolicy DefaultPolicy();

  void SetPolicy(GlassPolicy policy);
  void RefreshGlassPreference();
  void OnOsSettingsChanged(bool force_notify);

  GlassPolicy policy_ = DefaultPolicy();
  raw_ptr<PrefService> prefs_ = nullptr;
  ChangeCallback changed_;
  SystemSignalReader system_signal_reader_;
  base::CallbackListSubscription os_settings_subscription_;
  PrefChangeRegistrar pref_change_registrar_;
  base::WeakPtrFactory<AppearanceRuntimeSignalSource> weak_factory_{this};
};

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_RUNTIME_SIGNALS_H_
