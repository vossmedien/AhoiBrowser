// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <string_view>
#include <utility>

#include "ahoi/browser/session/portable_workspace_bundle.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"

namespace ahoi::session {
namespace {

std::optional<base::Uuid> ReadUuid(const base::DictValue& value,
                                   std::string_view key) {
  const std::string* text = value.FindString(key);
  if (!text) {
    return std::nullopt;
  }
  base::Uuid id = base::Uuid::ParseLowercase(*text);
  return id.is_valid() ? std::make_optional(std::move(id)) : std::nullopt;
}

bool ReadParent(const base::DictValue& value,
                std::optional<base::Uuid>* parent) {
  if (!value.Find("parent_id")) {
    parent->reset();
    return true;
  }
  *parent = ReadUuid(value, "parent_id");
  return parent->has_value();
}

bool ReadAccent(const base::DictValue& value, std::optional<uint32_t>* accent) {
  const base::Value* raw = value.Find("accent_argb");
  if (!raw) {
    accent->reset();
    return true;
  }
  const std::string* text = raw->GetIfString();
  uint32_t parsed = 0;
  if (!text || text->size() != 8 || !base::HexStringToUInt(*text, &parsed)) {
    return false;
  }
  *accent = parsed;
  return true;
}

std::optional<sync::SharedTabTarget> ReadTarget(const base::DictValue& value) {
  const std::string* kind = value.FindString("kind");
  if (!kind) {
    return std::nullopt;
  }
  sync::SharedTabTarget target;
  if (*kind == "web" && value.size() == 2u) {
    const std::string* url = value.FindString("url");
    if (!url) {
      return std::nullopt;
    }
    target.kind = sync::SharedTabTargetKind::kWeb;
    target.url = *url;
  } else if (*kind == "new-tab" && value.size() == 1u) {
    target.kind = sync::SharedTabTargetKind::kNewTab;
  } else {
    return std::nullopt;
  }
  return target;
}

std::optional<sync::SharedSplitMetadata> ReadSplit(
    const base::DictValue& value) {
  if (value.size() != 7u) {
    return std::nullopt;
  }
  const auto id = ReadUuid(value, "id");
  const auto workspace_id = ReadUuid(value, "workspace_id");
  const auto axis = value.FindInt("axis");
  const auto arrangement = value.FindInt("arrangement");
  const auto primary = value.FindInt("primary_ratio");
  const auto secondary = value.FindInt("secondary_ratio");
  const base::ListValue* members = value.FindList("member_ids");
  if (!id || !workspace_id || !axis || !arrangement || !primary || !secondary ||
      !members || members->size() < 2u || members->size() > 4u || *axis < 0 ||
      *axis > 1 || *arrangement < 0 || *arrangement > 2 || *primary < 0 ||
      *primary > 1'000'000 || *secondary < 0 || *secondary > 1'000'000) {
    return std::nullopt;
  }
  sync::SharedSplitMetadata split{
      .id = *id,
      .workspace_id = *workspace_id,
      .topology = {.axis = static_cast<sync::SharedSplitAxis>(*axis),
                   .arrangement =
                       static_cast<sync::SharedSplitArrangement>(*arrangement)},
      .ratios = {.primary = static_cast<uint32_t>(*primary),
                 .secondary = static_cast<uint32_t>(*secondary)},
  };
  for (const base::Value& member : *members) {
    const std::string* text = member.GetIfString();
    if (!text) {
      return std::nullopt;
    }
    base::Uuid parsed = base::Uuid::ParseLowercase(*text);
    if (!parsed.is_valid()) {
      return std::nullopt;
    }
    split.topology.member_ids.push_back(std::move(parsed));
  }
  return split;
}

std::optional<tab_tree::PortableWorkspaceNode> ReadNode(
    const base::DictValue& value) {
  const std::string* kind = value.FindString("kind");
  const std::string* title = value.FindString("title");
  const std::string* icon = value.FindString("icon");
  const std::string* sort_key = value.FindString("sort_key");
  const auto id = ReadUuid(value, "id");
  const auto workspace_id = ReadUuid(value, "workspace_id");
  std::optional<base::Uuid> parent;
  std::optional<uint32_t> accent;
  if (!kind || !title || !icon || !sort_key || !id || !workspace_id ||
      !ReadParent(value, &parent) || !ReadAccent(value, &accent)) {
    return std::nullopt;
  }
  const size_t optional_count = parent.has_value() + accent.has_value();
  tab_tree::PortableWorkspaceNode node{
      .id = *id,
      .workspace_id = *workspace_id,
      .parent_id = parent,
      .title = base::UTF8ToUTF16(*title),
      .icon = base::UTF8ToUTF16(*icon),
      .accent_argb = accent,
      .sort_key = *sort_key,
  };
  if (*kind == "folder") {
    if (value.size() != 6u + optional_count) {
      return std::nullopt;
    }
    node.type = tab_tree::TreeNodeType::kFolder;
    return node;
  }
  if (*kind != "page") {
    return std::nullopt;
  }
  const auto is_temporary = value.FindBool("is_temporary");
  const base::DictValue* target = value.FindDict("target");
  if (!is_temporary || !target ||
      value.size() !=
          8u + optional_count +
              static_cast<size_t>(value.Find("home_target") != nullptr)) {
    return std::nullopt;
  }
  node.type = tab_tree::TreeNodeType::kSavedPage;
  node.is_temporary = *is_temporary;
  node.target = ReadTarget(*target);
  if (!node.target) {
    return std::nullopt;
  }
  if (const base::DictValue* home = value.FindDict("home_target")) {
    node.home_target = ReadTarget(*home);
    if (!node.home_target) {
      return std::nullopt;
    }
  }
  return node;
}

std::optional<PortableArchive> ReadArchive(const base::DictValue& value) {
  const bool has_split = value.Find("split") != nullptr;
  if (value.size() != (has_split ? 6u : 5u)) {
    return std::nullopt;
  }
  const auto id = ReadUuid(value, "id");
  const auto workspace_id = ReadUuid(value, "workspace_id");
  const auto reason = value.FindInt("reason");
  const std::string* archived_at_us = value.FindString("archived_at_us");
  const base::ListValue* pages = value.FindList("pages");
  int64_t archived_us = 0;
  if (!id || !workspace_id || !reason || *reason < 0 || *reason > 1 ||
      !archived_at_us || !base::StringToInt64(*archived_at_us, &archived_us) ||
      !pages || pages->empty() || pages->size() > 4u) {
    return std::nullopt;
  }
  PortableArchive archive{
      .id = *id,
      .snapshot = {.workspace_id = *workspace_id},
      .reason = static_cast<sync::SharedArchiveReason>(*reason),
      .archived_at = base::Time::FromDeltaSinceWindowsEpoch(
          base::Microseconds(archived_us)),
  };
  for (const base::Value& raw : *pages) {
    const base::DictValue* page = raw.GetIfDict();
    if (!page || page->size() < 4u || page->size() > 6u) {
      return std::nullopt;
    }
    const auto page_id = ReadUuid(*page, "id");
    const std::string* sort_key = page->FindString("sort_key");
    const std::string* title = page->FindString("title");
    const base::DictValue* target = page->FindDict("target");
    std::optional<base::Uuid> parent;
    if (!page_id || !sort_key || !title || !target ||
        !ReadParent(*page, &parent) ||
        page->size() !=
            4u + static_cast<size_t>(parent.has_value()) +
                static_cast<size_t>(page->Find("home_target") != nullptr)) {
      return std::nullopt;
    }
    sync::SharedArchivePageSnapshot item{
        .tree_node_id = *page_id,
        .parent_id = parent,
        .sort_key = *sort_key,
        .title = *title,
    };
    const auto decoded_target = ReadTarget(*target);
    if (!decoded_target) {
      return std::nullopt;
    }
    item.target = *decoded_target;
    if (const base::DictValue* home = page->FindDict("home_target")) {
      item.home_target = ReadTarget(*home);
      if (!item.home_target) {
        return std::nullopt;
      }
    }
    archive.snapshot.pages.push_back(std::move(item));
  }
  if (has_split) {
    const base::DictValue* split = value.FindDict("split");
    if (!split) {
      return std::nullopt;
    }
    archive.snapshot.split = ReadSplit(*split);
    if (!archive.snapshot.split) {
      return std::nullopt;
    }
  }
  return archive;
}

}  // namespace

std::optional<PortableWorkspaceStructure> DecodePortableWorkspaceBundle(
    std::string_view bytes) {
  if (bytes.empty() || bytes.size() > kMaximumPortableWorkspaceBundleBytes) {
    return std::nullopt;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(bytes, base::JSON_PARSE_RFC);
  const base::DictValue* root = parsed ? parsed->GetIfDict() : nullptr;
  const std::string* format = root ? root->FindString("format") : nullptr;
  if (!root || root->size() != 6u || !format || *format != "ahoi-workspaces" ||
      root->FindInt("version") != kPortableWorkspaceBundleVersion) {
    return std::nullopt;
  }
  const base::ListValue* workspaces = root->FindList("workspaces");
  const base::ListValue* nodes = root->FindList("nodes");
  const base::ListValue* splits = root->FindList("splits");
  const base::ListValue* archives = root->FindList("archives");
  if (!workspaces || !nodes || !splits || !archives || workspaces->empty() ||
      workspaces->size() > 128u || nodes->size() > 10'000u ||
      splits->size() > 2'500u || archives->size() > 10'000u) {
    return std::nullopt;
  }

  PortableWorkspaceStructure result;
  for (const base::Value& raw : *workspaces) {
    const base::DictValue* value = raw.GetIfDict();
    if (!value || value->size() < 5u || value->size() > 6u) {
      return std::nullopt;
    }
    const auto id = ReadUuid(*value, "id");
    const std::string* name = value->FindString("name");
    const std::string* icon = value->FindString("icon");
    const std::string* sort_key = value->FindString("sort_key");
    const auto policy = value->FindInt("archive_policy");
    std::optional<uint32_t> accent;
    if (!id || !name || !icon || !sort_key || !policy ||
        !ReadAccent(*value, &accent) ||
        value->size() != 5u + static_cast<size_t>(accent.has_value())) {
      return std::nullopt;
    }
    result.tree.workspaces.push_back({
        .id = *id,
        .name = base::UTF8ToUTF16(*name),
        .icon = base::UTF8ToUTF16(*icon),
        .sort_key = *sort_key,
        .accent_argb = accent,
        .archive_policy = static_cast<sync::SharedArchivePolicy>(*policy),
    });
  }
  for (const base::Value& raw : *nodes) {
    const base::DictValue* value = raw.GetIfDict();
    if (!value) {
      return std::nullopt;
    }
    auto node = ReadNode(*value);
    if (!node) {
      return std::nullopt;
    }
    result.tree.nodes.push_back(std::move(*node));
  }
  for (const base::Value& raw : *splits) {
    const base::DictValue* value = raw.GetIfDict();
    auto split = value ? ReadSplit(*value) : std::nullopt;
    if (!split) {
      return std::nullopt;
    }
    result.splits.push_back(std::move(*split));
  }
  for (const base::Value& raw : *archives) {
    const base::DictValue* value = raw.GetIfDict();
    auto archive = value ? ReadArchive(*value) : std::nullopt;
    if (!archive) {
      return std::nullopt;
    }
    result.archives.push_back(std::move(*archive));
  }
  // The encoder is also the final cross-reference and allowlist validator.
  // A parsed file never becomes an import plan merely because JSON is valid.
  return EncodePortableWorkspaceBundle(result)
             ? std::make_optional(std::move(result))
             : std::nullopt;
}

}  // namespace ahoi::session
