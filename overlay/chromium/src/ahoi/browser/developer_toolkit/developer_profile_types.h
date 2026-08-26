// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_TYPES_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "url/origin.h"

namespace ahoi {

inline constexpr int kDeveloperProfileSchemaVersion = 2;
inline constexpr char kDeveloperProfilesPref[] =
    "ahoi.developer_toolkit.profiles";
// Pref/JSON shape: {"version": 2, "origins": {<origin>: <profile>}}.

inline constexpr size_t kMaxDeveloperProfiles = 256;
inline constexpr size_t kMaxDeveloperProfileNameBytes = 64;
inline constexpr size_t kMaxDeveloperCssBytes = 256 * 1024;
inline constexpr size_t kMaxDeveloperJavaScriptBytes = 256 * 1024;
inline constexpr size_t kMaxDeveloperUserAgentBytes = 512;
inline constexpr size_t kMaxDeveloperHeaderRules = 64;
inline constexpr size_t kMaxDeveloperHeaderNameBytes = 128;
inline constexpr size_t kMaxDeveloperHeaderValueBytes = 8192;
inline constexpr size_t kMaxDeveloperHeaderBytes = 128 * 1024;
inline constexpr size_t kMaxDeveloperAssets = 64;
inline constexpr size_t kMaxDeveloperAssetIdBytes = 80;
inline constexpr size_t kMaxDeveloperAssetNameBytes = 96;
inline constexpr size_t kMaxDeveloperAssetScopeBytes = 2048;
inline constexpr size_t kMaxDeveloperSecretReferenceBytes = 160;
inline constexpr uint32_t kDeveloperStyleCompilerVersion = 1;

enum class DeveloperAssetKind {
  kStyle,
  kJavaScript,
};

enum class DeveloperStyleLanguage {
  kCss,
  kLess,
  kSass,
};

enum class DeveloperAssetScopeKind {
  kCurrentTab,
  kOrigin,
  kDomain,
  kPath,
};

// `value` is intentionally explicit: a tab-session token, canonical origin,
// lower-case domain, or absolute path prefix depending on `kind`. This keeps
// scope matching deterministic and prevents an editor label from becoming a
// hidden URL pattern language.
struct DeveloperAssetScope {
  DeveloperAssetScopeKind kind = DeveloperAssetScopeKind::kOrigin;
  std::string value;

  bool operator==(const DeveloperAssetScope&) const = default;
};

enum class DeveloperAssetLifetime {
  kOnce,
  kReload,
  kRestart,
};

enum class DeveloperJavaScriptWorld {
  kIsolated,
  kMain,
};

struct DeveloperAsset {
  std::string id;
  std::string name;
  DeveloperAssetKind kind = DeveloperAssetKind::kStyle;
  DeveloperStyleLanguage style_language = DeveloperStyleLanguage::kCss;
  bool enabled = false;
  std::string source;
  // Derived, bounded output for LESS/SASS. Keeping the source and its compiled
  // CSS together lets reload/restart/sync apply the last successful compile
  // without loading a compiler process outside an open editor.
  std::string compiled_css;
  uint32_t compiled_style_version = 0;
  DeveloperAssetScope scope;
  DeveloperAssetLifetime lifetime = DeveloperAssetLifetime::kRestart;
  bool sync_enabled = false;
  DeveloperJavaScriptWorld javascript_world =
      DeveloperJavaScriptWorld::kIsolated;
  bool main_world_warning_accepted = false;

  bool operator==(const DeveloperAsset&) const = default;
};

enum class DeveloperHeaderAction {
  kSet,
  kRemove,
};

struct DeveloperHeaderRule {
  std::string name;
  std::string value;
  // Opaque identifier only. The referenced value belongs in macOS Keychain
  // and must be materialized into a transient copy before request dispatch.
  std::string secret_reference;
  DeveloperHeaderAction action = DeveloperHeaderAction::kSet;

  bool operator==(const DeveloperHeaderRule&) const = default;
};

// A profile is stored under one canonical HTTP(S) origin. Code and override
// values remain inert until their corresponding explicit enabled bit is true.
struct DeveloperProfile {
  std::string name;
  std::vector<DeveloperAsset> assets;

  bool user_agent_enabled = false;
  std::string user_agent;

  bool header_rules_enabled = false;
  bool header_rules_sync_enabled = false;
  std::vector<DeveloperHeaderRule> header_rules;

  // Response rules are separate from request rules so a profile can never
  // accidentally reinterpret an existing request-header override after an
  // upgrade. Enabling this switch is an explicit advanced-mode opt-in in the
  // native editor.
  bool response_header_rules_enabled = false;
  bool response_header_rules_sync_enabled = false;
  std::vector<DeveloperHeaderRule> response_header_rules;

  // Applied to all requests from the active origin by the existing URLLoader
  // throttle. This creates no timer or background process.
  bool cache_disabled = false;

  bool operator==(const DeveloperProfile&) const = default;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_TYPES_H_
