// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/browser_setting_catalog.h"

#include <cmath>

namespace ahoi::sync {
namespace {

using Type = base::Value::Type;

// These literals deliberately avoid dependencies on Chrome UI/Sync targets.
// Sources below refer to the pinned M152 tree; registration/type checks remain
// the native adapter's responsibility on each platform before
// observation/apply.
constexpr BrowserSettingDescriptor kCatalog[] = {
    {kBrowserSearchEngineSettingId, "navigation", Type::STRING, false},
    // ahoi/browser/ui/appearance/appearance_prefs.{h,cc}:
    // RegisterProfilePrefs. Preserve all five existing product IDs.
    {"ahoi.appearance.glass_enabled", "appearance", Type::BOOLEAN, false},
    {"ahoi.appearance.sidebar_page_tint_enabled", "appearance", Type::BOOLEAN,
     false},
    {"ahoi.navigation.floating_auto_hide_enabled", "navigation", Type::BOOLEAN,
     false},
    {"ahoi.navigation.floating_reveal_notch_enabled", "navigation",
     Type::BOOLEAN, false},
    {"ahoi.navigation.floating_auto_hide_delay_ms", "navigation", Type::INTEGER,
     false},

    // chrome/common/pref_names.h; chrome/browser/ui/browser_ui_prefs.cc.
    // Home target+selector are one configuration: transferring only the
    // selector could activate a different local URL on B. Keep BOTH local until
    // a reviewed atomic home configuration exists. Home button visibility is
    // independent. No homepage URL, session contents, toolbar histories or
    // window geometry.
    {"browser.show_home_button", "navigation", Type::BOOLEAN, true},
    {"browser.show_forward_button", "navigation", Type::BOOLEAN, true},
    {"browser.pin_split_tab_button", "navigation", Type::BOOLEAN, true},
    {"browser.split_view_drag_and_drop_enabled", "navigation", Type::BOOLEAN,
     true},

    // chrome/common/pref_names.h; DownloadPrefs::RegisterProfilePrefs and
    // PluginPrefsFactory::RegisterProfilePrefs. No local path/auto-open list.
    {"download.prompt_for_download", "downloads", Type::BOOLEAN, true},
    {"plugins.always_open_pdf_externally", "downloads", Type::BOOLEAN, true},

    // components/spellcheck/browser/pref_names.h + SpellcheckServiceFactory;
    // components/translate/core/browser/translate_pref_names.h +
    // browser_ui_prefs;
    // chrome/common/pref_names.h + PrefsTabHelper::RegisterProfilePrefs.
    // Translation offers are not auto-translate/site allowlists or
    // cloud-spelling
    // consent. The sole string setting has an explicit two-value allowlist.
    {"browser.enable_spellchecking", "language", Type::BOOLEAN, true},
    {"translate.enabled", "language", Type::BOOLEAN, true},
    {"intl.charset_default", "language", Type::STRING, true},

    // chrome/browser/ui/read_anything/read_anything_prefs.{h,cc}.
    // No arbitrary font names, OS voices, language dictionaries or usage state.
    {"settings.a11y.read_anything.font_scale", "reading", Type::DOUBLE, true},
    {"settings.a11y.read_anything.color_info", "reading", Type::INTEGER, true},
    {"settings.a11y.read_anything.line_spacing", "reading", Type::INTEGER,
     true},
    {"settings.a11y.read_anything.letter_spacing", "reading", Type::INTEGER,
     true},
    {"settings.a11y.read_anything.links_enabled", "reading", Type::BOOLEAN,
     true},
    {"settings.a11y.read_anything.images_enabled", "reading", Type::BOOLEAN,
     true},

    // chrome/common/pref_names.h; browser_ui_prefs.cc;
    // Profile::RegisterProfilePrefs;
    // prefetch/pref_names.cc + preloading/preloading_prefs.{h,cc}.
    // These are individual user options, not site grants/account consent.
    {"enable_do_not_track", "privacy", Type::BOOLEAN, true},
    {"search.suggest_enabled", "network", Type::BOOLEAN, true},
    {"net.network_prediction_options", "network", Type::INTEGER, true},
};

// Keep equal to ahoi/browser/ui/appearance/appearance_prefs.h, without taking
// that UI dependency. The native reader clamps to this same inclusive range.
constexpr double kMinimumAutoHideDelayMs = 100;
constexpr double kMaximumAutoHideDelayMs = 10000;

// chrome/common/read_anything/read_anything_util.cc: AdjustFontScale clamps
// kMinScale..kMaxScale. The native preference is a double, not an integer enum.
constexpr double kMinimumFontScale = 0.5;
constexpr double kMaximumFontScale = 4.5;

// chrome/browser/preloading/preloading_prefs.h: NetworkPredictionOptions.
// Value 1 is still the registered default and explicitly maps to Standard;
// accepting it preserves a current native value, not an old sync format.
constexpr int kNetworkPredictionValues[] = {0, 1, 2, 3};

// chrome/common/read_anything/read_anything.mojom and the exposed choices in
// chrome/renderer/accessibility/read_anything/read_anything_app_controller.cc.
// Exclude Colors::kLowContrastDeprecated (6) and *Spacing::kTightDeprecated
// (0).
constexpr int kReadingColorValues[] = {0, 1, 2, 3, 4, 5, 7, 8};
constexpr int kReadingSpacingValues[] = {1, 2, 3};

bool IsOneOf(double value, base::span<const int> allowed) {
  for (int candidate : allowed) {
    if (value == candidate) {
      return true;
    }
  }
  return false;
}

bool IsPermittedCharset(std::string_view charset) {
  // Blink TextCodecUtf8::RegisterEncodingNames and
  // TextCodecLatin1::RegisterEncodingNames define these exact canonical names.
  // components/components_locale_settings.grd also uses windows-1252 as the
  // IDS_DEFAULT_ENCODING fallback. No generic label, alias or path is admitted.
  return charset == "UTF-8" || charset == "windows-1252";
}

}  // namespace

base::span<const BrowserSettingDescriptor> GetBrowserSettingCatalog() {
  return base::span<const BrowserSettingDescriptor>(kCatalog);
}

const BrowserSettingDescriptor* FindBrowserSetting(std::string_view id) {
  for (const auto& descriptor : kCatalog) {
    if (descriptor.id == id) {
      return &descriptor;
    }
  }
  return nullptr;
}

bool ValidateBrowserSettingValue(std::string_view id,
                                 const base::Value& value) {
  const auto* descriptor = FindBrowserSetting(id);
  if (!descriptor) {
    return false;
  }
  if (value.is_none()) {
    return true;
  }
  if (descriptor->value_kind == Type::BOOLEAN) {
    return value.is_bool();
  }
  if (descriptor->value_kind == Type::STRING) {
    if (id == kBrowserSearchEngineSettingId && value.is_string()) {
      const auto& choice = value.GetString();
      return choice == "duckDuckGo" || choice == "google" || choice == "bing";
    }
    return id == "intl.charset_default" && value.is_string() &&
           IsPermittedCharset(value.GetString());
  }
  if ((descriptor->value_kind != Type::INTEGER &&
       descriptor->value_kind != Type::DOUBLE) ||
      (!value.is_int() && !value.is_double())) {
    return false;
  }
  const double number = value.is_int() ? value.GetInt() : value.GetDouble();
  if (!std::isfinite(number) || (descriptor->value_kind == Type::INTEGER &&
                                 std::trunc(number) != number)) {
    return false;
  }
  if (id == "ahoi.navigation.floating_auto_hide_delay_ms") {
    return number >= kMinimumAutoHideDelayMs &&
           number <= kMaximumAutoHideDelayMs;
  }
  if (id == "settings.a11y.read_anything.font_scale") {
    return number >= kMinimumFontScale && number <= kMaximumFontScale;
  }
  if (id == "net.network_prediction_options") {
    return IsOneOf(number, kNetworkPredictionValues);
  }
  if (id == "settings.a11y.read_anything.color_info") {
    return IsOneOf(number, kReadingColorValues);
  }
  if (id == "settings.a11y.read_anything.line_spacing" ||
      id == "settings.a11y.read_anything.letter_spacing") {
    return IsOneOf(number, kReadingSpacingValues);
  }
  return false;  // A future numeric descriptor must add its own verified rule.
}

}  // namespace ahoi::sync
