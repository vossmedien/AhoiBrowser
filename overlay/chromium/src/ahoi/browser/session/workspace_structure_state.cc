// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/workspace_structure_state.h"

#include <set>

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/workspace_structure_sync.h"
#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/pickle.h"
#include "base/strings/string_number_conversions.h"
#include "base/token.h"

namespace ahoi::session {
namespace {
std::string EncodeNodes(const std::vector<tab_tree::TreeNode>& nodes) {
  base::Pickle p;
  p.WriteInt(1);
  p.WriteUInt32(nodes.size());
  for (const auto& n : nodes) {
    p.WriteString(n.id.AsLowercaseString());
    p.WriteString(n.workspace_id.AsLowercaseString());
    p.WriteString(n.parent_id ? n.parent_id->AsLowercaseString() : "");
    p.WriteString16(n.title);
    p.WriteString(n.url.spec());
    p.WriteString(n.sort_key);
    p.WriteInt64(n.created_at.ToDeltaSinceWindowsEpoch().InMicroseconds());
    p.WriteInt64(n.modified_at.ToDeltaSinceWindowsEpoch().InMicroseconds());
    p.WriteBool(n.is_temporary);
    p.WriteInt(n.target_kind ? static_cast<int>(*n.target_kind) : -1);
    p.WriteString(n.local_scheme.value_or(""));
    p.WriteString(n.home_url.spec());
    p.WriteInt(n.home_target_kind ? static_cast<int>(*n.home_target_kind) : -1);
    p.WriteString(n.home_local_scheme.value_or(""));
  }
  return base::Base64Encode(p.AsBytes());
}

bool DecodeNodes(std::string_view encoded,
                 std::vector<tab_tree::TreeNode>* nodes) {
  if (encoded.size() > 8 * 1024 * 1024) {
    return false;
  }
  auto bytes = base::Base64Decode(encoded);
  if (!bytes) {
    return false;
  }
  auto p = base::PickleIterator::WithData(*bytes);
  int version;
  uint32_t count;
  if (!p.ReadInt(&version) || version != 1 || !p.ReadUInt32(&count) ||
      count > 4) {
    return false;
  }
  std::set<base::Uuid> ids;
  for (uint32_t i = 0; i < count; ++i) {
    tab_tree::TreeNode n;
    n.type = tab_tree::TreeNodeType::kSavedPage;
    std::string id, workspace, parent, url, scheme, home, home_scheme;
    int kind, home_kind;
    int64_t created, modified;
    if (!p.ReadString(&id) || !p.ReadString(&workspace) ||
        !p.ReadString(&parent) || !p.ReadString16(&n.title) ||
        !p.ReadString(&url) || !p.ReadString(&n.sort_key) ||
        !p.ReadInt64(&created) || !p.ReadInt64(&modified) ||
        !p.ReadBool(&n.is_temporary) || !p.ReadInt(&kind) ||
        !p.ReadString(&scheme) || !p.ReadString(&home) ||
        !p.ReadInt(&home_kind) || !p.ReadString(&home_scheme)) {
      return false;
    }
    n.id = base::Uuid::ParseLowercase(id);
    n.workspace_id = base::Uuid::ParseLowercase(workspace);
    if (!parent.empty()) {
      n.parent_id = base::Uuid::ParseLowercase(parent);
    }
    n.url = GURL(url);
    n.home_url = GURL(home);
    if (kind >= 0) {
      n.target_kind = static_cast<sync::SharedTabTargetKind>(kind);
    }
    if (!scheme.empty()) {
      n.local_scheme = scheme;
    }
    if (home_kind >= 0) {
      n.home_target_kind = static_cast<sync::SharedTabTargetKind>(home_kind);
    }
    if (!home_scheme.empty()) {
      n.home_local_scheme = home_scheme;
    }
    n.created_at =
        base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(created));
    n.modified_at =
        base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(modified));
    if (!n.id.is_valid() || !ids.insert(n.id).second ||
        !n.workspace_id.is_valid() ||
        (n.parent_id && !n.parent_id->is_valid()) || n.title.empty() ||
        n.sort_key.empty() || created <= 0 || modified <= 0 || kind < -1 ||
        kind > 2 || home_kind < -1 || home_kind > 2 || home_kind == 1 ||
        !tab_tree::GetSharedPageTarget(n) ||
        ((!home.empty() || home_kind >= 0 || !home_scheme.empty()) &&
         !tab_tree::GetSharedHomeTarget(n))) {
      return false;
    }
    nodes->push_back(std::move(n));
  }
  return p.ReachedEnd();
}

base::DictValue EncodeSplit(const sync::SharedSplitMetadata& s) {
  base::ListValue ids;
  for (const auto& id : s.topology.member_ids) {
    ids.Append(id.AsLowercaseString());
  }
  return base::DictValue()
      .Set("id", s.id.AsLowercaseString())
      .Set("workspace", s.workspace_id.AsLowercaseString())
      .Set("members", std::move(ids))
      .Set("axis", static_cast<int>(s.topology.axis))
      .Set("arrangement", static_cast<int>(s.topology.arrangement))
      .Set("primary", static_cast<int>(s.ratios.primary))
      .Set("secondary", static_cast<int>(s.ratios.secondary));
}
std::optional<sync::SharedSplitMetadata> DecodeSplit(const base::DictValue& d) {
  const auto* id = d.FindString("id");
  const auto* workspace = d.FindString("workspace");
  const auto* ids = d.FindList("members");
  auto axis = d.FindInt("axis");
  auto arrangement = d.FindInt("arrangement");
  auto primary = d.FindInt("primary");
  auto secondary = d.FindInt("secondary");
  if (d.size() != 7 || !id || !workspace || !ids || !axis || !arrangement ||
      !primary || !secondary || *primary < 0 || *secondary < 0) {
    return std::nullopt;
  }
  sync::SharedSplitMetadata s;
  s.id = base::Uuid::ParseLowercase(*id);
  s.workspace_id = base::Uuid::ParseLowercase(*workspace);
  s.topology.axis = static_cast<sync::SharedSplitAxis>(*axis);
  s.topology.arrangement =
      static_cast<sync::SharedSplitArrangement>(*arrangement);
  s.ratios = {static_cast<uint32_t>(*primary),
              static_cast<uint32_t>(*secondary)};
  for (const auto& value : *ids) {
    if (!value.is_string()) {
      return std::nullopt;
    }
    s.topology.member_ids.push_back(
        base::Uuid::ParseLowercase(value.GetString()));
  }
  return sync::ValidateSplitMetadata(s) ? std::make_optional(std::move(s))
                                        : std::nullopt;
}

bool ValidEntry(const WorkspaceStructureEntry& entry) {
  std::vector<tab_tree::TreeNode> validated_nodes;
  if (entry.private_nodes.size() > 4 ||
      !DecodeNodes(EncodeNodes(entry.private_nodes), &validated_nodes)) {
    return false;
  }
  const auto type = sync::GetEntityType(entry.record);
  const auto id = sync::GetEntityId(entry.record);
  for (const auto* payload :
       {&entry.baseline, &entry.pending, &entry.pending_expected}) {
    if (payload->empty()) {
      continue;
    }
    sync::SyncRecord decoded;
    std::string canonical;
    if (!sync::DeserializeRecord(type, *payload, &decoded) ||
        sync::GetEntityId(decoded) != id ||
        !sync::SerializeRecord(decoded, &canonical) || canonical != *payload) {
      return false;
    }
  }
  if ((entry.pending.empty() && !entry.pending_expected.empty()) ||
      (!entry.native_split_token.empty() &&
       !base::Token::FromString(entry.native_split_token))) {
    return false;
  }
  if (std::holds_alternative<sync::SplitGroupRecord>(entry.record)) {
    return entry.private_nodes.empty() && !entry.archived_locally &&
           !entry.restore_pending &&
           (!entry.observed_split ||
            (entry.observed_split->id == id &&
             sync::ValidateSplitMetadata(*entry.observed_split)));
  }
  const auto* archive = std::get_if<sync::TabArchiveEntryRecord>(&entry.record);
  if (!archive || entry.observed_split || !entry.native_split_token.empty() ||
      (entry.restore_pending && !entry.archived_locally) ||
      (entry.archived_locally && entry.private_nodes.empty())) {
    return false;
  }
  if (entry.private_nodes.empty()) {
    return true;
  }
  if (entry.private_nodes.size() != archive->snapshot.pages.size()) {
    return false;
  }
  for (size_t i = 0; i < entry.private_nodes.size(); ++i) {
    const auto& node = entry.private_nodes[i];
    if (node.id != archive->snapshot.pages[i].tree_node_id ||
        node.workspace_id != archive->snapshot.workspace_id ||
        !node.is_temporary) {
      return false;
    }
  }
  return true;
}
}  // namespace

std::optional<std::string> EncodeWorkspaceStructureState(
    const WorkspaceStructureState& state) {
  if (state.clock != sync::HlcStamp() && !sync::IsValidSyncClock(state.clock)) {
    return std::nullopt;
  }
  base::ListValue entries, hidden;
  std::set<base::Uuid> hidden_ids;
  for (const auto& [id, e] : state.entries) {
    std::string record;
    if (id != sync::GetEntityId(e.record) || !ValidEntry(e) ||
        !sync::SerializeRecord(e.record, &record) ||
        e.private_nodes.size() > 4) {
      return std::nullopt;
    }
    base::DictValue d;
    d.Set("type", static_cast<int>(sync::GetEntityType(e.record)))
        .Set("record", record)
        .Set("baseline", e.baseline)
        .Set("pending", e.pending)
        .Set("pending_expected", e.pending_expected)
        .Set("native_split_token", e.native_split_token)
        .Set("private_nodes", EncodeNodes(e.private_nodes))
        .Set("archived_locally", e.archived_locally)
        .Set("restore_pending", e.restore_pending);
    d.Set("observed_split", e.observed_split
                                ? base::Value(EncodeSplit(*e.observed_split))
                                : base::Value());
    if (e.archived_locally) {
      for (const auto& n : e.private_nodes) {
        hidden_ids.insert(n.id);
      }
    }
    entries.Append(std::move(d));
  }
  for (const auto& id : hidden_ids) {
    hidden.Append(id.AsLowercaseString());
  }
  return base::WriteJson(
      base::DictValue()
          .Set("schema", 1)
          .Set("entries", std::move(entries))
          .Set("hidden_nodes", std::move(hidden))
          .Set("clock_us", base::NumberToString(state.clock.physical_time_us))
          .Set("clock_logical", base::NumberToString(state.clock.logical))
          .Set("clock_device", state.clock.device_tiebreak));
}

std::optional<WorkspaceStructureState> DecodeWorkspaceStructureState(
    std::string_view value) {
  WorkspaceStructureState state;
  if (value.empty()) {
    return state;
  }
  if (value.size() > 32 * 1024 * 1024) {
    return std::nullopt;
  }
  const auto root = base::JSONReader::ReadDict(value, base::JSON_PARSE_RFC);
  if (!root || root->size() != 6 || root->FindInt("schema") != 1 ||
      !root->FindList("hidden_nodes")) {
    return std::nullopt;
  }
  const auto* entries = root->FindList("entries");
  const auto* us = root->FindString("clock_us");
  const auto* logical = root->FindString("clock_logical");
  const auto* device = root->FindString("clock_device");
  if (!entries || !us || !logical || !device ||
      !base::StringToInt64(*us, &state.clock.physical_time_us) ||
      !base::StringToUint(*logical, &state.clock.logical)) {
    return std::nullopt;
  }
  state.clock.device_tiebreak = *device;
  if (state.clock != sync::HlcStamp() && !sync::IsValidSyncClock(state.clock)) {
    return std::nullopt;
  }
  for (const auto& value_entry : *entries) {
    const auto* d = value_entry.GetIfDict();
    if (!d || d->size() != 10) {
      return std::nullopt;
    }
    auto type = d->FindInt("type");
    auto archived = d->FindBool("archived_locally");
    auto restore = d->FindBool("restore_pending");
    const auto* record = d->FindString("record");
    const auto* baseline = d->FindString("baseline");
    const auto* pending = d->FindString("pending");
    const auto* expected = d->FindString("pending_expected");
    const auto* token = d->FindString("native_split_token");
    const auto* nodes = d->FindString("private_nodes");
    if (!type || (*type != 13 && *type != 14) || !archived || !restore ||
        !record || !baseline || !pending || !expected || !token || !nodes) {
      return std::nullopt;
    }
    WorkspaceStructureEntry e;
    if (!sync::DeserializeRecord(static_cast<sync::EntityType>(*type), *record,
                                 &e.record) ||
        !DecodeNodes(*nodes, &e.private_nodes)) {
      return std::nullopt;
    }
    e.baseline = *baseline;
    e.pending = *pending;
    e.pending_expected = *expected;
    e.native_split_token = *token;
    e.archived_locally = *archived;
    e.restore_pending = *restore;
    const auto* observed = d->Find("observed_split");
    if (!observed) {
      return std::nullopt;
    }
    if (!observed->is_none()) {
      if (!observed->is_dict()) {
        return std::nullopt;
      }
      e.observed_split = DecodeSplit(observed->GetDict());
      if (!e.observed_split) {
        return std::nullopt;
      }
    }
    if (!ValidEntry(e) ||
        !state.entries.emplace(sync::GetEntityId(e.record), std::move(e))
             .second) {
      return std::nullopt;
    }
  }
  std::set<base::Uuid> expected_hidden;
  for (const auto& [id, entry] : state.entries) {
    if (entry.archived_locally) {
      for (const auto& node : entry.private_nodes)
        expected_hidden.insert(node.id);
    }
  }
  std::set<base::Uuid> hidden;
  for (const auto& item : *root->FindList("hidden_nodes")) {
    if (!item.is_string())
      return std::nullopt;
    const auto id = base::Uuid::ParseLowercase(item.GetString());
    if (!id.is_valid() || !hidden.insert(id).second)
      return std::nullopt;
  }
  if (hidden != expected_hidden)
    return std::nullopt;
  return state;
}
}  // namespace ahoi::session
