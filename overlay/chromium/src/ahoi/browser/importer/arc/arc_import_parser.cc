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

#include "ahoi/browser/importer/arc/arc_import_plan_builder.h"
#include "ahoi/browser/importer/arc/arc_import_source_model.h"
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

using internal::IsSafeImportUrl;
using internal::kItemIdDomain;
using internal::kMaxSplitMembers;
using internal::kWorkspaceIdDomain;
using internal::SortKey;
using internal::SourceItem;
using internal::SourceItemKind;
using internal::SourceSpace;
using internal::SpaceRootKind;

bool IsBoundedUtf8(std::string_view value, size_t max_bytes) {
  return !value.empty() && value.size() <= max_bytes &&
         base::IsStringUTF8(value);
}

class ArcParser {
 public:
  ArcParser(const ArcImportSnapshot& snapshot,
            const ArcImportPlanOptions& options)
      : snapshot_(snapshot), options_(options) {}

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
        !ParseSpaceOrder(*ordered_space_ids) || !ValidateGraph()) {
      return {.status = status_};
    }
    const ArcImportStatus status = internal::BuildArcImportPlan(
        spaces_, items_, ordered_space_ids_, *options_, &plan_);
    if (status != ArcImportStatus::kOk) {
      return {.status = status};
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

  const base::raw_ref<const ArcImportSnapshot> snapshot_;
  const base::raw_ref<const ArcImportPlanOptions> options_;
  ArcImportStatus status_ = ArcImportStatus::kInvalidJson;
  ArcImportPlan plan_;
  std::map<std::string, SourceSpace> spaces_;
  std::map<std::string, SourceItem> items_;
  std::vector<std::string> ordered_space_ids_;
};

}  // namespace

ArcParseResult ParseArcSnapshot(const ArcImportSnapshot& snapshot,
                                const ArcImportPlanOptions& options) {
  return ArcParser(snapshot, options).Parse();
}

}  // namespace ahoi::importer::arc
