// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_asset_codec.h"

#include <string>
#include <string_view>
#include <utility>

namespace ahoi {
namespace {

constexpr char kId[] = "id";
constexpr char kName[] = "name";
constexpr char kKind[] = "kind";
constexpr char kLanguage[] = "language";
constexpr char kEnabled[] = "enabled";
constexpr char kSource[] = "source";
constexpr char kCompiledCss[] = "compiled_css";
constexpr char kCompiledStyleVersion[] = "compiled_style_version";
constexpr char kScope[] = "scope";
constexpr char kScopeKind[] = "kind";
constexpr char kScopeValue[] = "value";
constexpr char kLifetime[] = "lifetime";
constexpr char kSyncEnabled[] = "sync_enabled";
constexpr char kWorld[] = "world";
constexpr char kMainWorldWarningAccepted[] = "main_world_warning_accepted";

std::string_view EncodeKind(DeveloperAssetKind value) {
  return value == DeveloperAssetKind::kStyle ? "style" : "javascript";
}

std::string_view EncodeLanguage(DeveloperStyleLanguage value) {
  switch (value) {
    case DeveloperStyleLanguage::kCss:
      return "css";
    case DeveloperStyleLanguage::kLess:
      return "less";
    case DeveloperStyleLanguage::kSass:
      return "sass";
  }
  return "css";
}

std::string_view EncodeScope(DeveloperAssetScopeKind value) {
  switch (value) {
    case DeveloperAssetScopeKind::kCurrentTab:
      return "tab";
    case DeveloperAssetScopeKind::kOrigin:
      return "origin";
    case DeveloperAssetScopeKind::kDomain:
      return "domain";
    case DeveloperAssetScopeKind::kPath:
      return "path";
  }
  return "origin";
}

std::string_view EncodeLifetime(DeveloperAssetLifetime value) {
  switch (value) {
    case DeveloperAssetLifetime::kOnce:
      return "once";
    case DeveloperAssetLifetime::kReload:
      return "reload";
    case DeveloperAssetLifetime::kRestart:
      return "restart";
  }
  return "restart";
}

std::string_view EncodeWorld(DeveloperJavaScriptWorld value) {
  return value == DeveloperJavaScriptWorld::kIsolated ? "isolated" : "main";
}

std::optional<DeveloperAssetKind> DecodeKind(std::string_view value) {
  if (value == "style") {
    return DeveloperAssetKind::kStyle;
  }
  if (value == "javascript") {
    return DeveloperAssetKind::kJavaScript;
  }
  return std::nullopt;
}

std::optional<DeveloperStyleLanguage> DecodeLanguage(std::string_view value) {
  if (value == "css") {
    return DeveloperStyleLanguage::kCss;
  }
  if (value == "less") {
    return DeveloperStyleLanguage::kLess;
  }
  if (value == "sass") {
    return DeveloperStyleLanguage::kSass;
  }
  return std::nullopt;
}

std::optional<DeveloperAssetScopeKind> DecodeScope(std::string_view value) {
  if (value == "tab") {
    return DeveloperAssetScopeKind::kCurrentTab;
  }
  if (value == "origin") {
    return DeveloperAssetScopeKind::kOrigin;
  }
  if (value == "domain") {
    return DeveloperAssetScopeKind::kDomain;
  }
  if (value == "path") {
    return DeveloperAssetScopeKind::kPath;
  }
  return std::nullopt;
}

std::optional<DeveloperAssetLifetime> DecodeLifetime(std::string_view value) {
  if (value == "once") {
    return DeveloperAssetLifetime::kOnce;
  }
  if (value == "reload") {
    return DeveloperAssetLifetime::kReload;
  }
  if (value == "restart") {
    return DeveloperAssetLifetime::kRestart;
  }
  return std::nullopt;
}

std::optional<DeveloperJavaScriptWorld> DecodeWorld(std::string_view value) {
  if (value == "isolated") {
    return DeveloperJavaScriptWorld::kIsolated;
  }
  if (value == "main") {
    return DeveloperJavaScriptWorld::kMain;
  }
  return std::nullopt;
}

}  // namespace

base::ListValue SerializeDeveloperAssets(
    const std::vector<DeveloperAsset>& assets) {
  base::ListValue result;
  for (const DeveloperAsset& asset : assets) {
    result.Append(
        base::DictValue()
            .Set(kId, asset.id)
            .Set(kName, asset.name)
            .Set(kKind, EncodeKind(asset.kind))
            .Set(kLanguage, EncodeLanguage(asset.style_language))
            .Set(kEnabled, asset.enabled)
            .Set(kSource, asset.source)
            .Set(kCompiledCss, asset.compiled_css)
            .Set(kCompiledStyleVersion,
                 static_cast<int>(asset.compiled_style_version))
            .Set(kScope, base::DictValue()
                             .Set(kScopeKind, EncodeScope(asset.scope.kind))
                             .Set(kScopeValue, asset.scope.value))
            .Set(kLifetime, EncodeLifetime(asset.lifetime))
            .Set(kSyncEnabled, asset.sync_enabled)
            .Set(kWorld, EncodeWorld(asset.javascript_world))
            .Set(kMainWorldWarningAccepted, asset.main_world_warning_accepted));
  }
  return result;
}

std::optional<std::vector<DeveloperAsset>> DeserializeDeveloperAssets(
    const base::ListValue& value) {
  if (value.size() > kMaxDeveloperAssets) {
    return std::nullopt;
  }
  std::vector<DeveloperAsset> result;
  result.reserve(value.size());
  for (const base::Value& entry : value) {
    const base::DictValue* dict = entry.GetIfDict();
    const base::DictValue* scope = dict ? dict->FindDict(kScope) : nullptr;
    const std::string* id = dict ? dict->FindString(kId) : nullptr;
    const std::string* name = dict ? dict->FindString(kName) : nullptr;
    const std::string* kind = dict ? dict->FindString(kKind) : nullptr;
    const std::string* language = dict ? dict->FindString(kLanguage) : nullptr;
    const std::optional<bool> enabled =
        dict ? dict->FindBool(kEnabled) : std::nullopt;
    const std::string* source = dict ? dict->FindString(kSource) : nullptr;
    const std::string* compiled_css =
        dict ? dict->FindString(kCompiledCss) : nullptr;
    const std::optional<int> compiled_style_version =
        dict ? dict->FindInt(kCompiledStyleVersion) : std::nullopt;
    const std::string* scope_kind =
        scope ? scope->FindString(kScopeKind) : nullptr;
    const std::string* scope_value =
        scope ? scope->FindString(kScopeValue) : nullptr;
    const std::string* lifetime = dict ? dict->FindString(kLifetime) : nullptr;
    const std::optional<bool> sync_enabled =
        dict ? dict->FindBool(kSyncEnabled) : std::nullopt;
    const std::string* world = dict ? dict->FindString(kWorld) : nullptr;
    const std::optional<bool> warning =
        dict ? dict->FindBool(kMainWorldWarningAccepted) : std::nullopt;
    if (!id || !name || !kind || !language || !enabled || !source ||
        !scope_kind || !scope_value || !lifetime || !sync_enabled || !world ||
        !warning) {
      return std::nullopt;
    }
    const auto parsed_kind = DecodeKind(*kind);
    const auto parsed_language = DecodeLanguage(*language);
    const auto parsed_scope = DecodeScope(*scope_kind);
    const auto parsed_lifetime = DecodeLifetime(*lifetime);
    const auto parsed_world = DecodeWorld(*world);
    if (!parsed_kind || !parsed_language || !parsed_scope || !parsed_lifetime ||
        !parsed_world) {
      return std::nullopt;
    }
    result.push_back({
        .id = *id,
        .name = *name,
        .kind = *parsed_kind,
        .style_language = *parsed_language,
        .enabled = *enabled,
        .source = *source,
        .compiled_css = compiled_css ? *compiled_css : std::string(),
        .compiled_style_version =
            compiled_style_version && *compiled_style_version >= 0
                ? static_cast<uint32_t>(*compiled_style_version)
                : 0u,
        .scope = {.kind = *parsed_scope, .value = *scope_value},
        .lifetime = *parsed_lifetime,
        .sync_enabled = *sync_enabled,
        .javascript_world = *parsed_world,
        .main_world_warning_accepted = *warning,
    });
  }
  return result;
}

std::vector<DeveloperAsset> MigrateLegacyDeveloperAssets(
    const url::Origin& owner_origin,
    bool css_enabled,
    std::string css_source,
    bool javascript_enabled,
    std::string javascript_source) {
  const std::string scope = owner_origin.Serialize();
  return {
      {
          .id = "legacy-style",
          .name = "Migrated CSS",
          .kind = DeveloperAssetKind::kStyle,
          .style_language = DeveloperStyleLanguage::kCss,
          .enabled = css_enabled,
          .source = std::move(css_source),
          .scope = {.kind = DeveloperAssetScopeKind::kOrigin, .value = scope},
      },
      {
          .id = "legacy-script",
          .name = "Migrated JavaScript",
          .kind = DeveloperAssetKind::kJavaScript,
          .enabled = javascript_enabled,
          .source = std::move(javascript_source),
          .scope = {.kind = DeveloperAssetScopeKind::kOrigin, .value = scope},
      },
  };
}

}  // namespace ahoi
