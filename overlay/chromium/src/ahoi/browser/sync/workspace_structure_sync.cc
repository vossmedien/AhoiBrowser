// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ahoi/browser/sync/workspace_structure_sync.h"
#include <algorithm>
#include <array>
#include <set>
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization_internal.h"
#include "ahoi/browser/sync/sync_unified_validation.h"
#include "base/hash/sha1.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

namespace ahoi::sync {
namespace {
bool Id(const base::Uuid& value) {
  return IsCanonicalSyncDeviceId(value.AsLowercaseString());
}
bool Target(const SharedTabTarget& t) {
  return ValidateSharedTarget(t.url, t.kind, t.local_scheme);
}
bool Text(std::string_view s, size_t bound) {
  return s.size() <= bound && s.find('\0') == std::string_view::npos &&
         base::IsStringUTF8(s);
}
}  // namespace
bool ValidateHomeTarget(const std::optional<SharedTabTarget>& value) {
  return !value ||
         (value->kind != SharedTabTargetKind::kNewTab && Target(*value));
}
bool ValidateSplitMetadata(const SharedSplitMetadata& v) {
  const auto n = v.topology.member_ids.size();
  std::set<base::Uuid> ids(v.topology.member_ids.begin(),
                           v.topology.member_ids.end());
  return Id(v.id) && Id(v.workspace_id) && n >= 2 && n <= 4 &&
         ids.size() == n && !ids.contains(v.id) && v.id != v.workspace_id &&
         std::ranges::all_of(ids, Id) &&
         (v.topology.axis == SharedSplitAxis::kHorizontal ||
          v.topology.axis == SharedSplitAxis::kVertical) &&
         v.topology.arrangement >= SharedSplitArrangement::kLinear &&
         v.topology.arrangement <= SharedSplitArrangement::kMainEnd &&
         (n == 3 ||
          v.topology.arrangement == SharedSplitArrangement::kLinear) &&
         v.ratios.primary <= SharedSplitRatios::kScale &&
         v.ratios.secondary <= SharedSplitRatios::kScale;
}
bool ValidateArchiveSnapshot(const SharedArchiveSnapshot& v) {
  if (!Id(v.workspace_id) || v.pages.empty() || v.pages.size() > 4 ||
      (v.pages.size() == 1) == v.split.has_value())
    return false;
  std::vector<base::Uuid> ids;
  for (const auto& p : v.pages) {
    if (!Id(p.tree_node_id) ||
        (p.parent_id && (!Id(*p.parent_id) || p.parent_id == p.tree_node_id)) ||
        p.sort_key.empty() || p.sort_key.size() > 1024 ||
        !std::ranges::all_of(
            p.sort_key,
            [](unsigned char c) { return c >= 0x21 && c <= 0x7e; }) ||
        !Text(p.title, 65536) || !Target(p.target) ||
        !ValidateHomeTarget(p.home_target))
      return false;
    ids.push_back(p.tree_node_id);
  }
  if (std::set<base::Uuid>(ids.begin(), ids.end()).size() != ids.size())
    return false;
  return !v.split || (ValidateSplitMetadata(*v.split) &&
                      v.split->workspace_id == v.workspace_id &&
                      v.split->topology.member_ids == ids);
}
base::Uuid ArchiveIdForSnapshot(const SharedArchiveSnapshot& v) {
  if (!ValidateArchiveSnapshot(v))
    return {};
  constexpr std::array<unsigned char, 16> ns = {
      0x6b, 0xa7, 0xb8, 0x11, 0x9d, 0xad, 0x11, 0xd1,
      0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};
  std::string material(ns.begin(), ns.end());
  material += std::string("ahoi:sync:archive:v1:") +
              (v.split ? "split:" : "page:") +
              (v.split ? v.split->id : v.pages.front().tree_node_id)
                  .AsLowercaseString();
  auto digest = base::SHA1HashString(material);
  digest.resize(16);
  digest[6] =
      static_cast<char>((static_cast<unsigned char>(digest[6]) & 0x0f) | 0x50);
  digest[8] =
      static_cast<char>((static_cast<unsigned char>(digest[8]) & 0x3f) | 0x80);
  const auto hex = base::ToLowerASCII(base::HexEncode(digest));
  return base::Uuid::ParseLowercase(hex.substr(0, 8) + "-" + hex.substr(8, 4) +
                                    "-" + hex.substr(12, 4) + "-" +
                                    hex.substr(16, 4) + "-" + hex.substr(20));
}
bool ValidateArchiveEntry(const TabArchiveEntryRecord& v) {
  return ValidateArchiveSnapshot(v.snapshot) &&
         v.id == ArchiveIdForSnapshot(v.snapshot) &&
         (v.reason == SharedArchiveReason::kAutomatic ||
          v.reason == SharedArchiveReason::kManual) &&
         v.archived_at.ToDeltaSinceWindowsEpoch().InMicroseconds() >=
             kMinimumSyncClockPhysicalUs;
}

namespace serialization_internal {
namespace {
Dict TargetDict(const SharedTabTarget& v) {
  Dict d;
  d.Set("url", v.url);
  d.Set("target_kind", static_cast<int>(v.kind));
  d.Set("local_scheme",
        v.local_scheme ? base::Value(*v.local_scheme) : base::Value());
  return d;
}
bool ReadTargetDict(const Dict& d, SharedTabTarget* v) {
  const auto kind = ReadInt32(d, "target_kind");
  const auto* scheme = d.Find("local_scheme");
  if (d.size() != 3 || !kind || *kind < 0 || *kind > 2 || !scheme ||
      (!scheme->is_none() && !scheme->is_string()) ||
      !ReadString(d, "url", &v->url))
    return false;
  v->kind = static_cast<SharedTabTargetKind>(*kind);
  v->local_scheme = scheme->is_string()
                        ? std::make_optional(scheme->GetString())
                        : std::nullopt;
  return Target(*v);
}
Dict TopologyDict(const SharedSplitTopology& v) {
  Dict d;
  base::ListValue ids;
  for (const auto& id : v.member_ids)
    ids.Append(id.AsLowercaseString());
  d.Set("member_ids", std::move(ids));
  d.Set("axis", static_cast<int>(v.axis));
  d.Set("arrangement", static_cast<int>(v.arrangement));
  return d;
}
Dict RatiosDict(const SharedSplitRatios& v) {
  Dict d;
  SetUInt32(d, "primary", v.primary);
  SetUInt32(d, "secondary", v.secondary);
  return d;
}
bool ReadTopology(const Dict& d, SharedSplitTopology* v) {
  const auto* ids = d.FindList("member_ids");
  const auto axis = ReadInt32(d, "axis"),
             arrangement = ReadInt32(d, "arrangement");
  if (d.size() != 3 || !ids || ids->size() < 2 || ids->size() > 4 || !axis ||
      *axis < 0 || *axis > 1 || !arrangement || *arrangement < 0 ||
      *arrangement > 2)
    return false;
  v->axis = static_cast<SharedSplitAxis>(*axis);
  v->arrangement = static_cast<SharedSplitArrangement>(*arrangement);
  for (const auto& raw : *ids) {
    if (!raw.is_string() || !IsCanonicalSyncDeviceId(raw.GetString()))
      return false;
    v->member_ids.push_back(base::Uuid::ParseLowercase(raw.GetString()));
  }
  return true;
}
bool ReadRatios(const Dict& d, SharedSplitRatios* v) {
  return d.size() == 2 && ReadUInt32(d, "primary", &v->primary) &&
         ReadUInt32(d, "secondary", &v->secondary);
}
Dict SplitDict(const SharedSplitMetadata& v) {
  Dict d;
  d.Set("id", v.id.AsLowercaseString());
  d.Set("workspace_id", v.workspace_id.AsLowercaseString());
  d.Set("topology", TopologyDict(v.topology));
  d.Set("ratios", RatiosDict(v.ratios));
  return d;
}
bool ReadSplit(const Dict& d, SharedSplitMetadata* v) {
  const auto* top = d.FindDict("topology");
  const auto* ratios = d.FindDict("ratios");
  return d.size() == 4 && top && ratios && ReadUuid(d, "id", &v->id, false) &&
         ReadUuid(d, "workspace_id", &v->workspace_id, false) &&
         ReadTopology(*top, &v->topology) && ReadRatios(*ratios, &v->ratios) &&
         ValidateSplitMetadata(*v);
}
Dict SnapshotDict(const SharedArchiveSnapshot& v) {
  Dict d;
  d.Set("workspace_id", v.workspace_id.AsLowercaseString());
  base::ListValue pages;
  for (const auto& p : v.pages) {
    Dict page;
    page.Set("tree_node_id", p.tree_node_id.AsLowercaseString());
    page.Set("parent_id", p.parent_id
                              ? base::Value(p.parent_id->AsLowercaseString())
                              : base::Value());
    page.Set("sort_key", p.sort_key);
    page.Set("title", p.title);
    page.Set("target", TargetDict(p.target));
    page.Set("home_target", p.home_target
                                ? base::Value(TargetDict(*p.home_target))
                                : base::Value());
    pages.Append(std::move(page));
  }
  d.Set("pages", std::move(pages));
  d.Set("split", v.split ? base::Value(SplitDict(*v.split)) : base::Value());
  return d;
}
bool ReadSnapshot(const Dict& d, SharedArchiveSnapshot* v) {
  const auto* pages = d.FindList("pages");
  const auto* split = d.Find("split");
  if (d.size() != 3 || !pages || pages->empty() || pages->size() > 4 ||
      !split || !ReadUuid(d, "workspace_id", &v->workspace_id, false))
    return false;
  for (const auto& raw : *pages) {
    const auto* p = raw.GetIfDict();
    SharedArchivePageSnapshot page;
    if (!p || p->size() != 6)
      return false;
    const auto* parent = p->Find("parent_id");
    const auto* target = p->FindDict("target");
    const auto* home = p->Find("home_target");
    if (!parent || !target || !home ||
        !ReadUuid(*p, "tree_node_id", &page.tree_node_id, false) ||
        !ReadString(*p, "sort_key", &page.sort_key) ||
        !ReadString(*p, "title", &page.title) ||
        !ReadTargetDict(*target, &page.target))
      return false;
    if (!parent->is_none()) {
      base::Uuid id;
      if (!ReadUuid(*p, "parent_id", &id, false))
        return false;
      page.parent_id = id;
    }
    if (!home->is_none()) {
      SharedTabTarget t;
      if (!home->is_dict() || !ReadTargetDict(home->GetDict(), &t))
        return false;
      page.home_target = t;
    }
    v->pages.push_back(std::move(page));
  }
  if (!split->is_none()) {
    SharedSplitMetadata s;
    if (!split->is_dict() || !ReadSplit(split->GetDict(), &s))
      return false;
    v->split = s;
  }
  return ValidateArchiveSnapshot(*v);
}
}  // namespace
void SetHomeTarget(Dict& d, const std::optional<SharedTabTarget>& t) {
  d.Set("home_url", t ? base::Value(t->url) : base::Value());
  d.Set("home_target_kind",
        t ? base::Value(static_cast<int>(t->kind)) : base::Value());
  d.Set("home_local_scheme",
        t && t->local_scheme ? base::Value(*t->local_scheme) : base::Value());
}
bool ReadHomeTarget(const Dict& d, std::optional<SharedTabTarget>* t) {
  const auto* url = d.Find("home_url");
  const auto* kind = d.Find("home_target_kind");
  const auto* scheme = d.Find("home_local_scheme");
  if (!url || !kind || !scheme)
    return false;
  if (url->is_none() && kind->is_none() && scheme->is_none()) {
    t->reset();
    return true;
  }
  Dict value;
  value.Set("url", url->Clone());
  value.Set("target_kind", kind->Clone());
  value.Set("local_scheme", scheme->Clone());
  SharedTabTarget target;
  if (!ReadTargetDict(value, &target))
    return false;
  *t = target;
  return ValidateHomeTarget(*t);
}
bool SerializeSplitGroup(const SplitGroupRecord& v, std::string* payload) {
  Dict d;
  SetCommon(d, v.model_version, v.id, v.tombstone, v.version, v.field_versions);
  d.Set("workspace_id", v.workspace_id.AsLowercaseString());
  d.Set("topology", TopologyDict(v.topology));
  d.Set("ratios", RatiosDict(v.ratios));
  return WriteDict(d, payload);
}
bool DeserializeSplitGroup(const Dict& d, SplitGroupRecord* v) {
  const auto* top = d.FindDict("topology");
  const auto* ratios = d.FindDict("ratios");
  return d.size() == 11 && top && ratios &&
         ReadCommon(d, &v->model_version, &v->id, &v->tombstone, &v->version,
                    &v->field_versions) &&
         ReadUuid(d, "workspace_id", &v->workspace_id, false) &&
         ReadTopology(*top, &v->topology) && ReadRatios(*ratios, &v->ratios);
}
bool SerializeArchiveEntry(const TabArchiveEntryRecord& v,
                           std::string* payload) {
  Dict d;
  SetCommon(d, v.model_version, v.id, v.tombstone, v.version, v.field_versions);
  d.Set("snapshot", SnapshotDict(v.snapshot));
  d.Set("reason", static_cast<int>(v.reason));
  SetTime(d, "archived_at", v.archived_at);
  d.Set("restored", v.restored);
  return WriteDict(d, payload) && payload->size() <= 512 * 1024;
}
bool DeserializeArchiveEntry(const Dict& d, TabArchiveEntryRecord* v) {
  const auto* snapshot = d.FindDict("snapshot");
  const auto reason = ReadInt32(d, "reason");
  const auto restored = d.FindBool("restored");
  if (d.size() != 12 || !snapshot || !reason || *reason < 0 || *reason > 1 ||
      !restored ||
      !ReadCommon(d, &v->model_version, &v->id, &v->tombstone, &v->version,
                  &v->field_versions) ||
      !ReadSnapshot(*snapshot, &v->snapshot) ||
      !ReadTime(d, "archived_at", &v->archived_at))
    return false;
  v->reason = static_cast<SharedArchiveReason>(*reason);
  v->restored = *restored;
  return true;
}
}  // namespace serialization_internal
}  // namespace ahoi::sync
