// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"

#include <utility>

#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "base/functional/bind.h"
#include "components/prefs/pref_service.h"
#include "ui/base/ui_base_features.h"
#include "ui/gfx/animation/animation.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/os_settings_provider.h"

namespace ahoi::appearance {

AppearanceRuntimeSignalSource::AppearanceRuntimeSignalSource(
    ChangeCallback changed)
    : AppearanceRuntimeSignalSource(nullptr,
                                    std::move(changed),
                                    SystemSignalReader()) {}

AppearanceRuntimeSignalSource::AppearanceRuntimeSignalSource(
    PrefService* prefs,
    ChangeCallback changed)
    : AppearanceRuntimeSignalSource(prefs,
                                    std::move(changed),
                                    SystemSignalReader()) {}

AppearanceRuntimeSignalSource::AppearanceRuntimeSignalSource(
    ChangeCallback changed,
    SystemSignalReader system_signal_reader)
    : AppearanceRuntimeSignalSource(nullptr,
                                    std::move(changed),
                                    std::move(system_signal_reader)) {}

AppearanceRuntimeSignalSource::AppearanceRuntimeSignalSource(
    PrefService* prefs,
    ChangeCallback changed,
    SystemSignalReader system_signal_reader)
    : prefs_(prefs),
      changed_(std::move(changed)),
      system_signal_reader_(std::move(system_signal_reader)) {
  if (prefs_ && prefs_->FindPreference(kGlassEnabledPref)) {
    pref_change_registrar_.Init(prefs_);
    pref_change_registrar_.Add(
        kGlassEnabledPref,
        base::BindRepeating(
            &AppearanceRuntimeSignalSource::RefreshGlassPreference,
            weak_factory_.GetWeakPtr()));
    RefreshGlassPreference();
  }
  if (system_signal_reader_.is_null()) {
    os_settings_subscription_ =
        ui::OsSettingsProvider::RegisterOsSettingsChangedCallback(
            base::BindRepeating(
                &AppearanceRuntimeSignalSource::OnOsSettingsChanged,
                weak_factory_.GetWeakPtr()));
  }
  RefreshSystemSignals();
}

AppearanceRuntimeSignalSource::~AppearanceRuntimeSignalSource() = default;

void AppearanceRuntimeSignalSource::RefreshSystemSignals() {
  const SystemAppearanceSignals signals = system_signal_reader_.is_null()
                                              ? ReadCurrentSystemSignals()
                                              : system_signal_reader_.Run();
  GlassPolicy policy = policy_;
  policy.system_reduce_transparency = signals.system_reduce_transparency;
  policy.high_contrast = signals.high_contrast;
  policy.reduced_motion = signals.reduced_motion;
  SetPolicy(policy);
}

void AppearanceRuntimeSignalSource::SetBatterySaver(bool battery_saver) {
  GlassPolicy policy = policy_;
  policy.battery_saver = battery_saver;
  SetPolicy(policy);
}

void AppearanceRuntimeSignalSource::SetPerformancePressure(
    PerformancePressure pressure) {
  GlassPolicy policy = policy_;
  policy.performance_pressure = pressure;
  SetPolicy(policy);
}

void AppearanceRuntimeSignalSource::SetEnabled(bool enabled) {
  GlassPolicy policy = policy_;
  policy.enabled = enabled;
  SetPolicy(policy);
}

void AppearanceRuntimeSignalSource::SetPlatformSupportsGlass(
    bool supports_glass) {
  GlassPolicy policy = policy_;
  policy.platform_supports_glass = supports_glass;
  SetPolicy(policy);
}

SystemAppearanceSignals
AppearanceRuntimeSignalSource::ReadCurrentSystemSignals() {
  const ui::OsSettingsProvider& settings = ui::OsSettingsProvider::Get();
  // Chromium caches this potentially expensive platform query. Refresh the
  // cache when the shared OS-settings notification fires so a macOS Reduce
  // Motion change is reflected without restarting the browser.
  gfx::Animation::UpdatePrefersReducedMotion();
  const ui::NativeTheme* native_theme =
      ui::NativeTheme::GetInstanceForNativeUi();
  return {
      .system_reduce_transparency = settings.PrefersReducedTransparency(),
      .high_contrast =
          settings.PreferredContrast() ==
              ui::NativeTheme::PreferredContrast::kMore ||
          (native_theme && native_theme->preferred_contrast() ==
                               ui::NativeTheme::PreferredContrast::kMore),
      .reduced_motion = gfx::Animation::PrefersReducedMotion(),
  };
}

GlassPolicy AppearanceRuntimeSignalSource::DefaultPolicy() {
  GlassPolicy policy;
  // Reuse Chromium's macOS/version feature gate. The Ahoi native bridge owns
  // the material surface while this policy owns accessibility and performance
  // fallbacks.
  policy.platform_supports_glass = features::IsGlassFrameEnabled();
  return policy;
}

void AppearanceRuntimeSignalSource::RefreshGlassPreference() {
  if (!prefs_) {
    return;
  }
  GlassPolicy policy = policy_;
  policy.enabled = IsGlassEnabled(*prefs_);
  SetPolicy(policy);
}

void AppearanceRuntimeSignalSource::SetPolicy(GlassPolicy policy) {
  if (policy_ == policy) {
    return;
  }
  policy_ = policy;
  if (changed_) {
    changed_.Run(policy_);
  }
}

void AppearanceRuntimeSignalSource::OnOsSettingsChanged(bool /*force_notify*/) {
  RefreshSystemSignals();
}

}  // namespace ahoi::appearance
