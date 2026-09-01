// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_parser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/memory/raw_ref.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "crypto/hash.h"
#include "url/gurl.h"

namespace ahoi::importer::arc {

namespace {

constexpr std::string_view kWorkspaceIdDomain = "arc-workspace-v1";
constexpr std::string_view kItemIdDomain = "arc-item-v1";
constexpr size_t kMaxSplitMembers = 4;

enum class SpaceRootKind {
  kPinned,
  kUnpinned,
};

enum class SourceItemKind {
  kContainer,
  kFolder,
  kSplit,
  kTab,
  kUnsupported,
};

struct SourceSpace {
  std::string id;
  std::string title;
  std::vector<std::string> root_container_ids;
};

struct SourceItem {
  std::string id;
  std::optional<std::string> parent_id;
  std::vector<std::string> children;
  SourceItemKind kind = SourceItemKind::kUnsupported;
  std::string title;
  std::string saved_title;
  std::string url;
  std::optional<std::string> container_space_id;
  bool is_top_apps_container = false;
  bool split_metadata_valid = false;
  ArcSplitOrientation split_orientation = ArcSplitOrientation::kHorizontal;
  std::optional<std::string> split_focus_item_id;
  std::map<std::string, double> split_width_factors;
};

bool IsBoundedUtf8(std::string_view value, size_t max_bytes) {
  return !value.empty() && value.size() <= max_bytes &&
         base::IsStringUTF8(value);
}

std::string SortKey(size_t index) {
  std::string digits = base::NumberToString(index);
  if (digits.size() < 10) {
    digits.insert(0, 10 - digits.size(), '0');
  }
  return digits;
}

bool IsSafeImportUrl(std::string_view value) {
  const GURL url(value);
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() &&
         !url.has_password();
}

class ArcParser {
 public:
  explicit ArcParser(const ArcImportSnapshot& snapshot) : snapshot_(snapshot) {}

  ArcParseResult Parse() {
    if (snapshot_->schema_version != kArcSnapshotSchemaVersion ||
        snapshot_->source_size < 0 ||
        static_cast<uint64_t>(snapshot_->source_size) !=
            snapshot_->json.size() ||
        snapshot_->json.size() > kMaxSnapshotBytes ||
        crypto::hash::Sha256(snapshot_->json) != snapshot_->sha256) {
      return {.status = ArcImportStatus::kSourceChanged};
    }

    std::optional<base::Value> parsed = base::JSONReader::Read(
        snapshot_->json, base::JSON_PARSE_RFC, kMaxTreeDepth + 16);
    if (!parsed.has_value() || !parsed->is_dict()) {
      return {.status = ArcImportStatus::kInvalidJson};
    }
    const base::DictValue& root = parsed->GetDict();
    const std::optional<int> source_version = root.FindInt("version");
    if (!source_version.has_value()) {
      return {.status = ArcImportStatus::kMissingRequiredField};
    }
    if (*source_version != kArcSourceSchemaVersion) {
      return {.status = ArcImportStatus::kUnsupportedSchema};
    }

    const base::DictValue* sync_state = root.FindDict("sidebarSyncState");
    const base::ListValue* space_models =
        sync_state ? sync_state->FindList("spaceModels") : nullptr;
    const base::ListValue* items =
        sync_state ? sync_state->FindList("items") : nullptr;
    const base::DictValue* container =
        sync_state ? sync_state->FindDict("container") : nullptr;
    const base::DictValue* container_value =
        container ? container->FindDict("value") : nullptr;
    const base::ListValue* ordered_space_ids =
        container_value ? container_value->FindList("orderedSpaceIDs")
                        : nullptr;
    if (!space_models || !items || !ordered_space_ids) {
      return {.status = ArcImportStatus::kMissingRequiredField};
    }

    if (!ParseSpaces(*space_models) || !ParseItems(*items) ||
        !ParseSpaceOrder(*ordered_space_ids) || !ValidateGraph() ||
        !BuildPlan()) {
      return {.status = status_};
    }
    return {.status = ArcImportStatus::kOk, .plan = std::move(plan_)};
  }

 private:
  bool Fail(ArcImportStatus status) {
    status_ = status;
    return false;
  }

  bool ValidateIdentifier(const std::string* identifier) {
    return identifier && IsBoundedUtf8(*identifier, kMaxSourceIdentifierBytes);
  }

  bool ReadOptionalTitle(const base::DictValue& value, std::string* title) {
    const base::Value* raw_title = value.Find("title");
    if (!raw_title || raw_title->is_none()) {
      title->clear();
      return true;
    }
    const std::string* text = raw_title->GetIfString();
    if (!text || text->size() > kMaxTitleBytes || !base::IsStringUTF8(*text)) {
      return Fail(text ? ArcImportStatus::kLimitExceeded
                       : ArcImportStatus::kInvalidText);
    }
    *title = *text;
    return true;
  }

  bool ReadIdentifierList(const base::ListValue& list,
                          size_t max_count,
                          std::vector<std::string>* identifiers) {
    if (list.size() > max_count) {
      return Fail(ArcImportStatus::kLimitExceeded);
    }
    std::set<std::string> seen;
    for (const base::Value& value : list) {
      const std::string* identifier = value.GetIfString();
      if (!ValidateIdentifier(identifier)) {
        return Fail(identifier ? ArcImportStatus::kLimitExceeded
                               : ArcImportStatus::kMalformedSerializedMap);
      }
      if (!seen.insert(*identifier).second) {
        return Fail(ArcImportStatus::kDuplicateIdentifier);
      }
      identifiers->push_back(*identifier);
    }
    return true;
  }

  bool ReadSpaceRoots(const base::DictValue& value,
                      std::vector<std::string>* roots) {
    const base::Value* new_container_ids_value =
        value.Find("newContainerIDs");
    const base::ListValue* new_container_ids =
        new_container_ids_value ? new_container_ids_value->GetIfList()
                                : nullptr;
    if (new_container_ids_value && !new_container_ids) {
      return Fail(ArcImportStatus::kMalformedSerializedMap);
    }
    if (new_container_ids) {
      if (new_container_ids->size() != 4) {
        return Fail(ArcImportStatus::kMalformedSerializedMap);
      }

      std::optional<std::string> pinned_root;
      std::optional<std::string> unpinned_root;
      std::set<std::string> seen_root_ids;
      for (size_t index = 0; index < new_container_ids->size(); index += 2) {
        const base::DictValue* selector =
            (*new_container_ids)[index].GetIfDict();
        const std::string* identifier =
            (*new_container_ids)[index + 1].GetIfString();
        if (!selector || !identifier) {
          return Fail(ArcImportStatus::kMalformedSerializedMap);
        }
        if (!ValidateIdentifier(identifier)) {
          return Fail(ArcImportStatus::kLimitExceeded);
        }
        if (!seen_root_ids.insert(*identifier).second) {
          return Fail(ArcImportStatus::kDuplicateIdentifier);
        }

        std::optional<SpaceRootKind> kind;
        if (selector->size() == 1) {
          if (const base::DictValue* pinned = selector->FindDict("pinned");
              pinned && pinned->empty()) {
            kind = SpaceRootKind::kPinned;
          } else if (const base::DictValue* unpinned =
                         selector->FindDict("unpinned");
                     unpinned && unpinned->size() == 1) {
            const base::DictValue* payload = unpinned->FindDict("_0");
            const base::DictValue* shared =
                payload && payload->size() == 1
                    ? payload->FindDict("shared")
                    : nullptr;
            if (shared && shared->empty()) {
              kind = SpaceRootKind::kUnpinned;
            }
          }
        }
        if (!kind.has_value()) {
          return Fail(ArcImportStatus::kMalformedSerializedMap);
        }

        std::optional<std::string>& destination =
            *kind == SpaceRootKind::kPinned ? pinned_root : unpinned_root;
        if (destination.has_value()) {
          return Fail(ArcImportStatus::kMalformedSerializedMap);
        }
        destination = *identifier;
      }
      if (!pinned_root.has_value() || !unpinned_root.has_value()) {
        return Fail(ArcImportStatus::kMalformedSerializedMap);
      }
      // Arc serializes a map, so source pair order is not semantic. Preserve a
      // stable product order with pinned items before unpinned items.
      roots->push_back(std::move(*pinned_root));
      roots->push_back(std::move(*unpinned_root));
      return true;
    }

    const base::ListValue* legacy_container_ids =
        value.FindList("containerIDs");
    if (!legacy_container_ids) {
      return Fail(ArcImportStatus::kMissingRequiredField);
    }
    if (legacy_container_ids->size() != 4) {
      return Fail(ArcImportStatus::kMalformedSerializedMap);
    }

    std::optional<std::string> pinned_root;
    std::optional<std::string> unpinned_root;
    std::set<std::string> seen_root_ids;
    for (size_t index = 0; index < legacy_container_ids->size(); index += 2) {
      const std::string* selector =
          (*legacy_container_ids)[index].GetIfString();
      const std::string* identifier =
          (*legacy_container_ids)[index + 1].GetIfString();
      if (!selector || !identifier ||
          (*selector != "pinned" && *selector != "unpinned")) {
        return Fail(ArcImportStatus::kMalformedSerializedMap);
      }
      if (!ValidateIdentifier(identifier)) {
        return Fail(ArcImportStatus::kLimitExceeded);
      }
      if (!seen_root_ids.insert(*identifier).second) {
        return Fail(ArcImportStatus::kDuplicateIdentifier);
      }
      std::optional<std::string>& destination =
          *selector == "pinned" ? pinned_root : unpinned_root;
      if (destination.has_value()) {
        return Fail(ArcImportStatus::kMalformedSerializedMap);
      }
      destination = *identifier;
    }
    if (!pinned_root.has_value() || !unpinned_root.has_value()) {
      return Fail(ArcImportStatus::kMalformedSerializedMap);
    }
    roots->push_back(std::move(*pinned_root));
    roots->push_back(std::move(*unpinned_root));
    return true;
  }

  bool ParseSpaces(const base::ListValue& serialized) {
    if (serialized.size() % 2 != 0 ||
        serialized.size() / 2 > kMaxWorkspaceCount) {
      return Fail(serialized.size() / 2 > kMaxWorkspaceCount
                      ? ArcImportStatus::kLimitExceeded
                      : ArcImportStatus::kMalformedSerializedMap);
    }
    for (size_t index = 0; index < serialized.size(); index += 2) {
      const std::string* map_key = serialized[index].GetIfString();
      const base::DictValue* wrapper = serialized[index + 1].GetIfDict();
      const base::DictValue* value =
          wrapper ? wrapper->FindDict("value") : nullptr;
      const std::string* id = value ? value->FindString("id") : nullptr;
      const std::string* title = value ? value->FindString("title") : nullptr;
      if (!ValidateIdentifier(map_key) || !ValidateIdentifier(id) ||
          *map_key != *id || !title || title->size() > kMaxTitleBytes ||
          !base::IsStringUTF8(*title)) {
        return Fail(!map_key || !id || !value || !wrapper
                        ? ArcImportStatus::kMalformedSerializedMap
                        : ArcImportStatus::kInvalidText);
      }
      SourceSpace space{.id = *id, .title = *title};
      if (!ReadSpaceRoots(*value, &space.root_container_ids)) {
        return false;
      }
      if (!spaces_.emplace(space.id, std::move(space)).second) {
        return Fail(ArcImportStatus::kDuplicateIdentifier);
      }
    }
    plan_.stats.source_workspace_count = spaces_.size();
    if (spaces_.empty()) {
      return Fail(ArcImportStatus::kNoImportableWorkspaces);
    }
    return true;
  }

  bool ParseChildren(const base::DictValue& value,
                     std::vector<std::string>* children) {
    const base::ListValue* child_ids = value.FindList("childrenIds");
    if (!child_ids) {
      return Fail(ArcImportStatus::kMissingRequiredField);
    }
    return ReadIdentifierList(*child_ids, kMaxChildrenPerItem, children);
  }

  bool ParseSplitData(const base::DictValue& split, SourceItem* item) {
    item->kind = SourceItemKind::kSplit;
    item->split_metadata_valid = true;

    const std::string* orientation = split.FindString("layoutOrientation");
    if (!orientation) {
      item->split_metadata_valid = false;
    } else if (*orientation == "horizontal") {
      item->split_orientation = ArcSplitOrientation::kHorizontal;
    } else if (*orientation == "vertical") {
      item->split_orientation = ArcSplitOrientation::kVertical;
    } else {
      item->split_metadata_valid = false;
    }

    const std::string* focus_item_id = split.FindString("focusItemID");
    if (!focus_item_id) {
      item->split_metadata_valid = false;
    } else if (!ValidateIdentifier(focus_item_id)) {
      return Fail(ArcImportStatus::kLimitExceeded);
    } else {
      item->split_focus_item_id = *focus_item_id;
    }

    const base::ListValue* factors = split.FindList("itemWidthFactors");
    if (!factors) {
      return true;
    }
    if (factors->size() > 2 * kMaxSplitMembers) {
      return Fail(ArcImportStatus::kLimitExceeded);
    }
    if (factors->size() % 2 != 0) {
      item->split_metadata_valid = false;
      return true;
    }
    for (size_t index = 0; index < factors->size(); index += 2) {
      const std::string* child_id = (*factors)[index].GetIfString();
      const std::optional<double> factor = (*factors)[index + 1].GetIfDouble();
      if (!ValidateIdentifier(child_id)) {
        return Fail(child_id ? ArcImportStatus::kLimitExceeded
                             : ArcImportStatus::kMalformedSerializedMap);
      }
      if (!factor.has_value() || !std::isfinite(*factor) || *factor <= 0.0) {
        item->split_metadata_valid = false;
        continue;
      }
      if (!item->split_width_factors.emplace(*child_id, *factor).second) {
        return Fail(ArcImportStatus::kDuplicateIdentifier);
      }
    }
    return true;
  }

  bool ParseItemData(const base::DictValue& data, SourceItem* item) {
    if (data.size() != 1) {
      return Fail(ArcImportStatus::kGraphViolation);
    }
    if (const base::DictValue* tab = data.FindDict("tab")) {
      item->kind = SourceItemKind::kTab;
      const std::string* url = tab->FindString("savedURL");
      if (!url || url->size() > kMaxUrlBytes || !base::IsStringUTF8(*url)) {
        return Fail(url ? ArcImportStatus::kLimitExceeded
                        : ArcImportStatus::kMissingRequiredField);
      }
      item->url = *url;
      if (const std::string* saved_title = tab->FindString("savedTitle")) {
        if (saved_title->size() > kMaxTitleBytes ||
            !base::IsStringUTF8(*saved_title)) {
          return Fail(ArcImportStatus::kInvalidText);
        }
        item->saved_title = *saved_title;
      }
      return true;
    }
    if (data.FindDict("list")) {
      item->kind = SourceItemKind::kFolder;
      return true;
    }
    if (const base::DictValue* split = data.FindDict("splitView")) {
      return ParseSplitData(*split, item);
    }
    if (const base::DictValue* item_container =
            data.FindDict("itemContainer")) {
      item->kind = SourceItemKind::kContainer;
      const base::DictValue* container_type =
          item_container->FindDict("containerType");
      const base::DictValue* space_items =
          container_type ? container_type->FindDict("spaceItems") : nullptr;
      if (space_items) {
        const std::string* space_id = space_items->FindString("_0");
        if (!ValidateIdentifier(space_id)) {
          return Fail(ArcImportStatus::kMalformedSerializedMap);
        }
        item->container_space_id = *space_id;
      } else if (!container_type || !container_type->FindDict("topApps")) {
        return Fail(ArcImportStatus::kMalformedSerializedMap);
      } else {
        item->is_top_apps_container = true;
      }
      return true;
    }
    item->kind = SourceItemKind::kUnsupported;
    return true;
  }

  bool ParseItems(const base::ListValue& serialized) {
    if (serialized.size() % 2 != 0 || serialized.size() / 2 > kMaxItemCount) {
      return Fail(serialized.size() / 2 > kMaxItemCount
                      ? ArcImportStatus::kLimitExceeded
                      : ArcImportStatus::kMalformedSerializedMap);
    }
    for (size_t index = 0; index < serialized.size(); index += 2) {
      const std::string* map_key = serialized[index].GetIfString();
      const base::DictValue* wrapper = serialized[index + 1].GetIfDict();
      const base::DictValue* value =
          wrapper ? wrapper->FindDict("value") : nullptr;
      const std::string* id = value ? value->FindString("id") : nullptr;
      const base::DictValue* data = value ? value->FindDict("data") : nullptr;
      if (!ValidateIdentifier(map_key) || !ValidateIdentifier(id) ||
          *map_key != *id || !value || !data) {
        return Fail(ArcImportStatus::kMalformedSerializedMap);
      }

      SourceItem item{.id = *id};
      const base::Value* parent = value->Find("parentID");
      if (!parent || (!parent->is_none() && !parent->is_string())) {
        return Fail(ArcImportStatus::kMissingRequiredField);
      }
      if (const std::string* parent_id = parent->GetIfString()) {
        if (!ValidateIdentifier(parent_id)) {
          return Fail(ArcImportStatus::kLimitExceeded);
        }
        item.parent_id = *parent_id;
      }
      if (!ReadOptionalTitle(*value, &item.title) ||
          !ParseChildren(*value, &item.children) ||
          !ParseItemData(*data, &item)) {
        return false;
      }
      if (!items_.emplace(item.id, std::move(item)).second) {
        return Fail(ArcImportStatus::kDuplicateIdentifier);
      }
    }
    plan_.stats.source_item_count = items_.size();
    return true;
  }

  bool ParseSpaceOrder(const base::ListValue& ordered_space_ids) {
    if (!ReadIdentifierList(ordered_space_ids, kMaxWorkspaceCount,
                            &ordered_space_ids_)) {
      return false;
    }
    if (ordered_space_ids_.size() != spaces_.size()) {
      return Fail(ArcImportStatus::kGraphViolation);
    }
    for (const std::string& id : ordered_space_ids_) {
      if (!spaces_.contains(id)) {
        return Fail(ArcImportStatus::kGraphViolation);
      }
    }
    return true;
  }

  std::optional<ArcSplitDescriptor> BuildSplitDescriptor(
      const SourceItem& item,
      const base::Uuid& folder_id) const {
    if (!item.split_metadata_valid || item.children.size() < 2 ||
        item.children.size() > kMaxSplitMembers ||
        !item.split_focus_item_id.has_value() ||
        std::find(item.children.begin(), item.children.end(),
                  *item.split_focus_item_id) == item.children.end()) {
      return std::nullopt;
    }

    std::set<std::string> child_ids(item.children.begin(), item.children.end());
    for (const auto& [factor_id, factor] : item.split_width_factors) {
      if (!child_ids.contains(factor_id) || !std::isfinite(factor) ||
          factor <= 0.0) {
        return std::nullopt;
      }
    }

    ArcSplitDescriptor descriptor{
        .folder_node_id = folder_id,
        .orientation = item.split_orientation,
        .focused_member_node_id =
            MakeDeterministicArcId(kItemIdDomain, *item.split_focus_item_id),
    };
    std::vector<double> weights(item.children.size(), 0.0);
    double known_total = 0.0;
    size_t missing_count = 0;
    for (size_t index = 0; index < item.children.size(); ++index) {
      const auto child_it = items_.find(item.children[index]);
      if (child_it == items_.end() ||
          child_it->second.kind != SourceItemKind::kTab ||
          !child_it->second.children.empty() ||
          !IsSafeImportUrl(child_it->second.url)) {
        return std::nullopt;
      }
      descriptor.member_node_ids.push_back(
          MakeDeterministicArcId(kItemIdDomain, child_it->second.id));
      const auto factor_it = item.split_width_factors.find(child_it->second.id);
      if (factor_it == item.split_width_factors.end()) {
        ++missing_count;
      } else {
        weights[index] = factor_it->second;
        known_total += factor_it->second;
      }
    }
    if (missing_count > 0) {
      const double inferred =
          known_total > 0.0 && known_total < 1.0
              ? (1.0 - known_total) / static_cast<double>(missing_count)
              : (known_total > 0.0
                     ? known_total / static_cast<double>(item.children.size() -
                                                         missing_count)
                     : 1.0);
      for (double& weight : weights) {
        if (weight == 0.0) {
          weight = inferred;
        }
      }
    }
    const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
    if (!std::isfinite(total) || total <= 0.0) {
      return std::nullopt;
    }
    for (double weight : weights) {
      descriptor.normalized_ratios.push_back(weight / total);
    }
    return descriptor;
  }

  bool ValidateAcyclic(const std::string& item_id,
                       size_t depth,
                       std::map<std::string, int>* states) {
    if (depth > kMaxTreeDepth) {
      return Fail(ArcImportStatus::kLimitExceeded);
    }
    const int state = (*states)[item_id];
    if (state == 1) {
      return Fail(ArcImportStatus::kGraphViolation);
    }
    if (state == 2) {
      return true;
    }
    (*states)[item_id] = 1;
    const auto item_it = items_.find(item_id);
    if (item_it == items_.end()) {
      return Fail(ArcImportStatus::kGraphViolation);
    }
    for (const std::string& child_id : item_it->second.children) {
      if (!ValidateAcyclic(child_id, depth + 1, states)) {
        return false;
      }
    }
    (*states)[item_id] = 2;
    return true;
  }

  bool ValidateGraph() {
    for (const auto& [id, item] : items_) {
      for (const std::string& child_id : item.children) {
        const auto child_it = items_.find(child_id);
        if (child_it == items_.end() ||
            child_it->second.parent_id != std::optional<std::string>(id)) {
          return Fail(ArcImportStatus::kGraphViolation);
        }
      }
      if (item.parent_id.has_value()) {
        const auto parent_it = items_.find(*item.parent_id);
        if (parent_it == items_.end() ||
            std::find(parent_it->second.children.begin(),
                      parent_it->second.children.end(),
                      id) == parent_it->second.children.end()) {
          return Fail(ArcImportStatus::kGraphViolation);
        }
      }
    }

    std::map<std::string, int> states;
    for (const auto& entry : items_) {
      if (!ValidateAcyclic(entry.first, /*depth=*/0, &states)) {
        return false;
      }
    }
    return true;
  }

  bool VisitItem(const std::string& item_id,
                 const std::string& expected_parent_id,
                 const base::Uuid& workspace_id,
                 std::optional<base::Uuid> destination_parent_id,
                 std::string sort_key,
                 size_t depth,
                 bool emit) {
    if (depth > kMaxTreeDepth) {
      return Fail(ArcImportStatus::kLimitExceeded);
    }
    const auto item_it = items_.find(item_id);
    if (item_it == items_.end() || !item_it->second.parent_id.has_value() ||
        *item_it->second.parent_id != expected_parent_id ||
        !claimed_item_ids_.insert(item_id).second) {
      return Fail(ArcImportStatus::kGraphViolation);
    }
    const SourceItem& item = item_it->second;
    if (item.kind == SourceItemKind::kContainer) {
      return Fail(ArcImportStatus::kGraphViolation);
    }

    if (!emit || item.kind == SourceItemKind::kUnsupported) {
      ++plan_.stats.skipped_unsupported_item_count;
      size_t ignored_position = 0;
      for (const std::string& child_id : item.children) {
        if (!VisitItem(child_id, item.id, workspace_id, destination_parent_id,
                       SortKey(ignored_position++), depth + 1,
                       /*emit=*/false)) {
          return false;
        }
      }
      return true;
    }

    if (item.kind == SourceItemKind::kTab) {
      if (!item.children.empty()) {
        return Fail(ArcImportStatus::kGraphViolation);
      }
      const GURL url(item.url);
      if (!IsSafeImportUrl(item.url)) {
        ++plan_.stats.skipped_unsafe_url_count;
        return true;
      }
      std::string title = item.title.empty() ? item.saved_title : item.title;
      if (title.empty()) {
        title = std::string(url.host());
        if (title.empty()) {
          title = "Imported Page";
        }
      }
      plan_.tree.nodes.push_back(tab_tree::TreeNode{
          .id = MakeDeterministicArcId(kItemIdDomain, item.id),
          .workspace_id = workspace_id,
          .parent_id = destination_parent_id,
          .type = tab_tree::TreeNodeType::kSavedPage,
          .title = base::UTF8ToUTF16(title),
          .url = url,
          .sort_key = std::move(sort_key),
          .created_at = base::Time::UnixEpoch(),
          .modified_at = base::Time::UnixEpoch(),
      });
      ++plan_.stats.imported_page_count;
      return true;
    }

    const base::Uuid folder_id = MakeDeterministicArcId(kItemIdDomain, item.id);
    const std::optional<ArcSplitDescriptor> split_descriptor =
        item.kind == SourceItemKind::kSplit
            ? BuildSplitDescriptor(item, folder_id)
            : std::nullopt;
    std::string title = item.title;
    if (title.empty()) {
      title = item.kind == SourceItemKind::kSplit ? "Split View"
                                                  : "Untitled Folder";
    }
    plan_.tree.nodes.push_back(tab_tree::TreeNode{
        .id = folder_id,
        .workspace_id = workspace_id,
        .parent_id = destination_parent_id,
        .type = tab_tree::TreeNodeType::kFolder,
        .title = base::UTF8ToUTF16(title),
        .icon = split_descriptor.has_value() ? u"split" : u"folder",
        .sort_key = std::move(sort_key),
        .created_at = base::Time::UnixEpoch(),
        .modified_at = base::Time::UnixEpoch(),
    });
    ++plan_.stats.imported_folder_count;

    size_t child_position = 0;
    for (const std::string& child_id : item.children) {
      if (!VisitItem(child_id, item.id, workspace_id, folder_id,
                     SortKey(child_position++), depth + 1, /*emit=*/true)) {
        return false;
      }
    }
    if (item.kind == SourceItemKind::kSplit) {
      if (split_descriptor.has_value()) {
        plan_.splits.push_back(*split_descriptor);
        ++plan_.stats.imported_split_count;
      } else {
        plan_.degraded_split_folder_node_ids.push_back(folder_id);
        ++plan_.stats.degraded_split_count;
      }
    }
    return true;
  }

  bool BuildGlobalTopApps(const base::Uuid& workspace_id,
                          size_t* top_level_position) {
    const SourceItem* top_apps = nullptr;
    for (const auto& [id, item] : items_) {
      if (!item.is_top_apps_container) {
        continue;
      }
      if (top_apps || item.parent_id.has_value() ||
          !claimed_item_ids_.insert(id).second) {
        return Fail(ArcImportStatus::kGraphViolation);
      }
      top_apps = &item;
    }
    if (!top_apps || top_apps->children.empty()) {
      return true;
    }

    const base::Uuid folder_id =
        MakeDeterministicArcId(kItemIdDomain, top_apps->id);
    plan_.tree.nodes.push_back(tab_tree::TreeNode{
        .id = folder_id,
        .workspace_id = workspace_id,
        .type = tab_tree::TreeNodeType::kFolder,
        .title = u"Arc Favorites",
        .icon = u"star",
        .sort_key = SortKey((*top_level_position)++),
        .created_at = base::Time::UnixEpoch(),
        .modified_at = base::Time::UnixEpoch(),
    });
    ++plan_.stats.imported_folder_count;
    const size_t nodes_before = plan_.tree.nodes.size();
    const size_t pages_before = plan_.stats.imported_page_count;
    size_t child_position = 0;
    for (const std::string& child_id : top_apps->children) {
      if (!VisitItem(child_id, top_apps->id, workspace_id, folder_id,
                     SortKey(child_position++), /*depth=*/1,
                     /*emit=*/true)) {
        return false;
      }
    }
    plan_.stats.imported_global_top_app_count +=
        plan_.stats.imported_page_count - pages_before;
    for (size_t index = nodes_before; index < plan_.tree.nodes.size();
         ++index) {
      if (plan_.tree.nodes[index].type == tab_tree::TreeNodeType::kSavedPage) {
        plan_.global_top_app_page_node_ids.push_back(
            plan_.tree.nodes[index].id);
      }
    }
    return true;
  }

  bool BuildWorkspace(const SourceSpace& space,
                      size_t workspace_position,
                      bool include_global_top_apps) {
    std::string title =
        space.title.empty() ? "Imported Workspace" : space.title;
    const base::Uuid workspace_id =
        MakeDeterministicArcId(kWorkspaceIdDomain, space.id);
    if (!workspace_id.is_valid()) {
      return Fail(ArcImportStatus::kInvalidText);
    }
    plan_.tree.workspaces.push_back(tab_tree::Workspace{
        .id = workspace_id,
        .name = base::UTF8ToUTF16(title),
        .sort_key = SortKey(workspace_position),
        .created_at = base::Time::UnixEpoch(),
        .modified_at = base::Time::UnixEpoch(),
    });
    ++plan_.stats.imported_workspace_count;

    size_t top_level_position = 0;
    if (include_global_top_apps &&
        !BuildGlobalTopApps(workspace_id, &top_level_position)) {
      return false;
    }
    for (const std::string& root_id : space.root_container_ids) {
      const auto root_it = items_.find(root_id);
      if (root_it == items_.end() ||
          root_it->second.kind != SourceItemKind::kContainer ||
          root_it->second.parent_id.has_value() ||
          !root_it->second.container_space_id.has_value() ||
          *root_it->second.container_space_id != space.id ||
          !claimed_item_ids_.insert(root_id).second) {
        return Fail(ArcImportStatus::kGraphViolation);
      }
      for (const std::string& child_id : root_it->second.children) {
        if (!VisitItem(child_id, root_id, workspace_id, std::nullopt,
                       SortKey(top_level_position++), /*depth=*/1,
                       /*emit=*/true)) {
          return false;
        }
      }
    }
    return true;
  }

  bool BuildPlan() {
    for (size_t index = 0; index < ordered_space_ids_.size(); ++index) {
      const auto space_it = spaces_.find(ordered_space_ids_[index]);
      if (space_it == spaces_.end() ||
          !BuildWorkspace(space_it->second, index,
                          /*include_global_top_apps=*/index == 0)) {
        return false;
      }
    }
    if (claimed_item_ids_.size() != items_.size()) {
      return Fail(ArcImportStatus::kGraphViolation);
    }
    return true;
  }

  const base::raw_ref<const ArcImportSnapshot> snapshot_;
  ArcImportStatus status_ = ArcImportStatus::kInvalidJson;
  ArcImportPlan plan_;
  std::map<std::string, SourceSpace> spaces_;
  std::map<std::string, SourceItem> items_;
  std::vector<std::string> ordered_space_ids_;
  std::set<std::string> claimed_item_ids_;
};

}  // namespace

ArcParseResult ParseArcSnapshot(const ArcImportSnapshot& snapshot) {
  return ArcParser(snapshot).Parse();
}

}  // namespace ahoi::importer::arc
