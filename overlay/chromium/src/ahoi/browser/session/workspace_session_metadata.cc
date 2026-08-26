// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/workspace_session_metadata.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"

namespace ahoi::session {

namespace {

constexpr size_t kMaximumSerializedMetadataBytes = 1024;
constexpr char kVersionKey[] = "version";
constexpr char kActiveWorkspaceIdKey[] = "active_workspace_id";
constexpr char kWorkspaceIdKey[] = "workspace_id";
constexpr char kTreeNodeIdKey[] = "tree_node_id";
constexpr char kLastActiveInWorkspaceKey[] = "last_active_in_workspace";

struct ParsedPayload {
  std::optional<base::Value> root;
  SessionMetadataDecodeResult result = SessionMetadataDecodeResult::kMalformed;
};

ParsedPayload ParseCurrentVersionPayload(std::string_view serialized) {
  ParsedPayload parsed;
  if (serialized.empty() ||
      serialized.size() > kMaximumSerializedMetadataBytes) {
    return parsed;
  }

  parsed.root = base::JSONReader::Read(serialized, base::JSON_PARSE_RFC);
  if (!parsed.root.has_value()) {
    return parsed;
  }
  const base::DictValue* dictionary = parsed.root->GetIfDict();
  if (!dictionary) {
    return parsed;
  }

  const std::optional<int> version = dictionary->FindInt(kVersionKey);
  if (!version.has_value()) {
    return parsed;
  }
  if (*version != kWorkspaceSessionMetadataVersion) {
    parsed.result = SessionMetadataDecodeResult::kUnsupportedVersion;
    return parsed;
  }

  parsed.result = SessionMetadataDecodeResult::kSuccess;
  return parsed;
}

std::optional<base::Uuid> ParseRequiredUuid(const base::DictValue& dictionary,
                                            std::string_view key) {
  const std::string* serialized = dictionary.FindString(key);
  if (!serialized) {
    return std::nullopt;
  }
  base::Uuid uuid = base::Uuid::ParseLowercase(*serialized);
  return uuid.is_valid() ? std::make_optional(std::move(uuid)) : std::nullopt;
}

}  // namespace

std::optional<std::string> EncodeWindowSessionMetadata(
    const WindowSessionMetadata& metadata) {
  if (!metadata.active_workspace_id.is_valid()) {
    return std::nullopt;
  }

  base::DictValue dictionary;
  dictionary.Set(kVersionKey, kWorkspaceSessionMetadataVersion);
  dictionary.Set(kActiveWorkspaceIdKey,
                 metadata.active_workspace_id.AsLowercaseString());
  return base::WriteJson(dictionary);
}

SessionMetadataDecodeResult DecodeWindowSessionMetadata(
    std::string_view serialized,
    WindowSessionMetadata* metadata) {
  if (!metadata) {
    return SessionMetadataDecodeResult::kMalformed;
  }

  ParsedPayload parsed = ParseCurrentVersionPayload(serialized);
  if (parsed.result != SessionMetadataDecodeResult::kSuccess) {
    return parsed.result;
  }
  const base::DictValue* dictionary = parsed.root->GetIfDict();
  if (!dictionary || dictionary->size() != 2u) {
    return SessionMetadataDecodeResult::kMalformed;
  }

  std::optional<base::Uuid> active_workspace_id =
      ParseRequiredUuid(*dictionary, kActiveWorkspaceIdKey);
  if (!active_workspace_id.has_value()) {
    return SessionMetadataDecodeResult::kMalformed;
  }

  *metadata = WindowSessionMetadata{.active_workspace_id =
                                        std::move(*active_workspace_id)};
  return SessionMetadataDecodeResult::kSuccess;
}

std::optional<std::string> EncodeTabSessionMetadata(
    const TabSessionMetadata& metadata) {
  if (!metadata.workspace_id.is_valid() ||
      (metadata.tree_node_id.has_value() &&
       !metadata.tree_node_id->is_valid())) {
    return std::nullopt;
  }

  base::DictValue dictionary;
  dictionary.Set(kVersionKey, kWorkspaceSessionMetadataVersion);
  dictionary.Set(kWorkspaceIdKey, metadata.workspace_id.AsLowercaseString());
  if (metadata.tree_node_id.has_value()) {
    dictionary.Set(kTreeNodeIdKey, metadata.tree_node_id->AsLowercaseString());
  }
  dictionary.Set(kLastActiveInWorkspaceKey, metadata.last_active_in_workspace);
  return base::WriteJson(dictionary);
}

SessionMetadataDecodeResult DecodeTabSessionMetadata(
    std::string_view serialized,
    TabSessionMetadata* metadata) {
  if (!metadata) {
    return SessionMetadataDecodeResult::kMalformed;
  }

  ParsedPayload parsed = ParseCurrentVersionPayload(serialized);
  if (parsed.result != SessionMetadataDecodeResult::kSuccess) {
    return parsed.result;
  }

  const base::DictValue* dictionary = parsed.root->GetIfDict();
  if (!dictionary) {
    return SessionMetadataDecodeResult::kMalformed;
  }
  const base::Value* tree_node_value = dictionary->Find(kTreeNodeIdKey);
  const size_t expected_field_count = tree_node_value ? 4u : 3u;
  if (dictionary->size() != expected_field_count) {
    return SessionMetadataDecodeResult::kMalformed;
  }

  std::optional<base::Uuid> workspace_id =
      ParseRequiredUuid(*dictionary, kWorkspaceIdKey);
  const std::optional<bool> last_active =
      dictionary->FindBool(kLastActiveInWorkspaceKey);
  if (!workspace_id.has_value() || !last_active.has_value()) {
    return SessionMetadataDecodeResult::kMalformed;
  }

  std::optional<base::Uuid> tree_node_id;
  if (tree_node_value) {
    tree_node_id = ParseRequiredUuid(*dictionary, kTreeNodeIdKey);
    if (!tree_node_id.has_value()) {
      return SessionMetadataDecodeResult::kMalformed;
    }
  }

  *metadata = TabSessionMetadata{
      .workspace_id = std::move(*workspace_id),
      .tree_node_id = std::move(tree_node_id),
      .last_active_in_workspace = *last_active,
  };
  return SessionMetadataDecodeResult::kSuccess;
}

std::optional<base::Uuid> ResolveWorkspaceForRestore(
    const std::optional<base::Uuid>& requested_workspace,
    base::span<const base::Uuid> available_workspaces) {
  if (requested_workspace.has_value() && requested_workspace->is_valid() &&
      std::ranges::find(available_workspaces, *requested_workspace) !=
          available_workspaces.end()) {
    return requested_workspace;
  }

  const auto fallback = std::ranges::find_if(
      available_workspaces, [](const base::Uuid& id) { return id.is_valid(); });
  return fallback == available_workspaces.end() ? std::nullopt
                                                : std::make_optional(*fallback);
}

}  // namespace ahoi::session
