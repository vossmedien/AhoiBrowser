// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/sync/sync_merge.h"

#include <algorithm>
#include <functional>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace ahoi::sync {
namespace {

void SetError(const char* message, std::string* error) {
  if (error) {
    *error = message;
  }
}

bool ValidVersion(const SyncVersion& version, std::string* error) {
  if (version.model_version <= 0 ||
      version.model_version > kCurrentModelVersion ||
      version.stamp.physical_time_us < 0 ||
      version.stamp.device_tiebreak.empty()) {
    SetError("invalid version", error);
    return false;
  }
  return true;
}

bool ValidUuid(const base::Uuid& uuid, const char* field, std::string* error) {
  if (!uuid.is_valid()) {
    SetError(field, error);
    return false;
  }
  return true;
}

bool ValidUrl(const std::string& url, const char* field, std::string* error) {
  const GURL parsed(url);
  if (!parsed.is_valid() || !parsed.SchemeIsHTTPOrHTTPS() ||
      parsed.host().empty() || parsed.has_username() || parsed.has_password()) {
    SetError(field, error);
    return false;
  }
  return true;
}

bool ValidText(std::string_view value,
               size_t maximum_bytes,
               bool allow_empty,
               const char* field,
               std::string* error) {
  if ((!allow_empty && value.empty()) || value.size() > maximum_bytes ||
      value.find('\0') != std::string_view::npos ||
      !base::IsStringUTF8(value)) {
    SetError(field, error);
    return false;
  }
  return true;
}

bool ValidExtensionId(std::string_view extension_id) {
  return extension_id.size() == 32u &&
         std::ranges::all_of(extension_id, [](char value) {
           return value >= 'a' && value <= 'p';
         });
}

// Header values never have a representation in the sync model. A shareable
// header profile can therefore carry only explicit remove rules; set/append
// rules and arbitrary extra fields stay in the local Developer Toolkit store.
bool ValidMetadataOnlyHeaderProfile(std::string_view source) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(source, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  const base::DictValue& root = parsed->GetDict();
  if (root.size() != 2u || root.FindInt("version") != 1) {
    return false;
  }
  const base::ListValue* rules = root.FindList("rules");
  if (!rules || rules->size() > 100u) {
    return false;
  }
  for (const base::Value& rule_value : *rules) {
    const base::DictValue* rule = rule_value.GetIfDict();
    const std::string* action = rule ? rule->FindString("action") : nullptr;
    if (!rule || rule->size() != 2u || !action || *action != "remove") {
      return false;
    }
    const std::string* name = rule->FindString("name");
    if (!name || name->empty() || name->size() > 128u ||
        !std::ranges::all_of(*name, [](unsigned char value) {
          return base::IsAsciiAlphaNumeric(value) || value == '-';
        })) {
      return false;
    }
  }
  return true;
}

}  // namespace

MergeDecision DecideMerge(const SyncVersion& existing_version,
                          const std::string& existing_payload,
                          const SyncVersion& incoming_version,
                          const std::string& incoming_payload) {
  if (incoming_version > existing_version) {
    return MergeDecision::kAcceptIncoming;
  }
  if (incoming_version < existing_version) {
    return MergeDecision::kKeepExisting;
  }
  return existing_payload == incoming_payload ? MergeDecision::kDuplicate
                                              : MergeDecision::kInvalid;
}

bool ValidateRecord(const SyncRecord& record, std::string* error) {
  SyncRecord normalized = record;
  if (!NormalizeFieldVersions(&normalized, error)) {
    return false;
  }
  const FieldVersionMap& normalized_versions = std::visit(
      [](const auto& value) -> const FieldVersionMap& {
        return value.field_versions;
      },
      normalized);
  for (const auto& [field, stamp] : normalized_versions) {
    if (stamp > GetVersion(record).stamp) {
      SetError("field version exceeds record version", error);
      return false;
    }
  }
  return std::visit(
      [error](const auto& value) {
        if (value.model_version <= 0 ||
            value.model_version > kCurrentModelVersion) {
          SetError("unsupported model version", error);
          return false;
        }
        if (value.model_version != value.version.model_version) {
          SetError("record/version model mismatch", error);
          return false;
        }
        if (!ValidUuid(value.id, "invalid entity id", error) ||
            !ValidVersion(value.version, error)) {
          return false;
        }

        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, DeviceRecord>) {
          if (value.type < DeviceType::kMacDesktop ||
              value.type > DeviceType::kOther) {
            SetError("invalid device type", error);
            return false;
          }
          return true;
        } else if constexpr (std::is_same_v<T, WorkspaceRecord>) {
          return true;
        } else if constexpr (std::is_same_v<T, TreeNodeRecord>) {
          if (!ValidUuid(value.workspace_id, "invalid workspace id", error) ||
              (value.parent_id &&
               !ValidUuid(*value.parent_id, "invalid parent id", error))) {
            return false;
          }
          if (value.kind == TreeNodeKind::kPage &&
              !ValidUrl(value.url, "invalid page url", error)) {
            return false;
          }
          if (value.kind == TreeNodeKind::kFolder && !value.url.empty()) {
            SetError("folder contains url", error);
            return false;
          }
          return value.kind == TreeNodeKind::kFolder ||
                 value.kind == TreeNodeKind::kPage;
        } else if constexpr (std::is_same_v<T, HistoryRecord>) {
          if (!ValidUrl(value.url, "invalid history url", error)) {
            return false;
          }
          if (value.visit_count < 0) {
            SetError("negative visit count", error);
            return false;
          }
          return true;
        } else if constexpr (std::is_same_v<T, RemoteTabRecord>) {
          if (!ValidUuid(value.device_id, "invalid tab device id", error) ||
              !ValidUuid(value.session_id, "invalid tab session id", error) ||
              (value.workspace_id &&
               !ValidUuid(*value.workspace_id, "invalid tab workspace id",
                          error))) {
            return false;
          }
          if (value.is_incognito) {
            SetError("incognito tabs are local-only", error);
            return false;
          }
          return ValidUrl(value.url, "invalid tab url", error);
        } else if constexpr (std::is_same_v<T, DeviceSessionRecord>) {
          return ValidUuid(value.device_id, "invalid session device id", error);
        } else if constexpr (std::is_same_v<T, RemoteCommandRecord>) {
          if (!ValidUuid(value.source_device_id, "invalid command source",
                         error) ||
              !ValidUuid(value.target_device_id, "invalid command target",
                         error) ||
              value.source_device_id == value.target_device_id ||
              value.issued_at.is_null() || value.expires_at.is_null() ||
              value.expires_at - value.issued_at != base::Minutes(5)) {
            SetError("invalid command envelope", error);
            return false;
          }
          std::string nonce;
          std::string signature;
          if (!base::Base64Decode(value.nonce_base64, &nonce) ||
              nonce.size() < 16u || nonce.size() > 64u ||
              !base::Base64Decode(value.signature_base64, &signature) ||
              signature.size() != 64u || value.result_code.size() > 64u) {
            SetError("invalid command authentication", error);
            return false;
          }
          switch (value.kind) {
            case RemoteCommandKind::kOpen:
              if (value.tab_id ||
                  !ValidUrl(value.url, "invalid command url", error)) {
                return false;
              }
              break;
            case RemoteCommandKind::kFocus:
            case RemoteCommandKind::kClose:
              if (!value.tab_id || value.workspace_id || !value.url.empty()) {
                SetError("invalid single-tab command", error);
                return false;
              }
              break;
          }
          return true;
        } else if constexpr (std::is_same_v<T, AppearanceRecord>) {
          if (value.color_mode != "system" && value.color_mode != "light" &&
              value.color_mode != "dark") {
            SetError("invalid appearance color mode", error);
            return false;
          }
          if (value.use_system_accent && value.accent_argb) {
            SetError("system accent contains custom color", error);
            return false;
          }
          return true;
        } else if constexpr (std::is_same_v<T, PermittedSettingRecord>) {
          if (!ValidText(value.setting_id, 128u, false,
                         "invalid permitted setting id", error) ||
              !ValidText(value.value_json, 8192u, false,
                         "invalid permitted setting value", error) ||
              !base::JSONReader::Read(value.value_json, base::JSON_PARSE_RFC)) {
            SetError("invalid permitted setting value", error);
            return false;
          }
          return true;
        } else if constexpr (std::is_same_v<T, ExtensionInventoryRecord>) {
          if (!ValidUuid(value.device_id, "invalid inventory device", error) ||
              !ValidExtensionId(value.extension_id) ||
              !ValidText(value.name, 256u, true,
                         "invalid extension inventory name", error) ||
              !ValidText(value.extension_version, 64u, true,
                         "invalid extension inventory version", error)) {
            SetError("invalid extension inventory", error);
            return false;
          }
          return true;
        } else {
          static_assert(std::is_same_v<T, DeveloperAssetRecord>);
          if (value.kind < DeveloperAssetKind::kCss ||
              value.kind > DeveloperAssetKind::kHeaderProfile ||
              (!value.tombstone && !value.opted_in) ||
              !ValidText(value.name, 256u, value.tombstone,
                         "invalid developer asset name", error) ||
              !ValidText(value.scope, 2048u, value.tombstone,
                         "invalid developer asset scope", error) ||
              !ValidText(value.source, 512u * 1024u, value.tombstone,
                         "invalid developer asset source", error)) {
            SetError("invalid developer asset", error);
            return false;
          }
          if (value.kind == DeveloperAssetKind::kHeaderProfile &&
              !value.tombstone &&
              !ValidMetadataOnlyHeaderProfile(value.source)) {
            SetError("header profile contains non-shareable material", error);
            return false;
          }
          return true;
        }
      },
      record);
}

bool ValidateTreeGraph(const std::vector<TreeNodeRecord>& nodes,
                       std::string* error) {
  std::unordered_map<base::Uuid, const TreeNodeRecord*, base::UuidHash> all;
  std::unordered_map<base::Uuid, const TreeNodeRecord*, base::UuidHash> active;
  for (const TreeNodeRecord& node : nodes) {
    if (!ValidateRecord(node, error)) {
      return false;
    }
    if (!all.emplace(node.id, &node).second) {
      SetError("duplicate tree node", error);
      return false;
    }
    if (node.tombstone) {
      continue;
    }
    active.emplace(node.id, &node);
  }

  for (const auto& [id, node] : active) {
    if (!node->parent_id) {
      continue;
    }
    if (*node->parent_id == id) {
      SetError("tree node is its own parent", error);
      return false;
    }
    const auto parent = all.find(*node->parent_id);
    // A deleted folder remains a valid historical parent. This avoids a
    // provider page getting stuck when a parent tombstone arrives before its
    // descendants; the sidebar still hides any branch rooted below it.
    if (parent == all.end() ||
        parent->second->workspace_id != node->workspace_id ||
        parent->second->kind != TreeNodeKind::kFolder) {
      SetError("invalid tree parent", error);
      return false;
    }
  }

  std::unordered_map<base::Uuid, uint8_t, base::UuidHash> colors;
  std::function<bool(const base::Uuid&)> visit = [&](const base::Uuid& id) {
    uint8_t& color = colors[id];
    if (color == 1) {
      SetError("tree cycle", error);
      return false;
    }
    if (color == 2) {
      return true;
    }
    color = 1;
    const auto it = active.find(id);
    if (it != active.end() && it->second->parent_id &&
        !visit(*it->second->parent_id)) {
      return false;
    }
    color = 2;
    return true;
  };

  for (const auto& [id, node] : active) {
    if (!visit(id)) {
      return false;
    }
  }
  return true;
}

}  // namespace ahoi::sync
