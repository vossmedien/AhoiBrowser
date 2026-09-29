// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/bookmark_native_observation.h"

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization_internal.h"

namespace ahoi::sync {

bool SerializeNativeBookmarkObservation(const BookmarkRecord& observation,
                                        std::string* payload) {
  if (!payload || observation.tombstone ||
      !ValidateNativeBookmarkShape(observation)) {
    return false;
  }
  serialization_internal::Dict value;
  value.Set("native_observation", true);
  value.Set("id", observation.id.AsLowercaseString());
  value.Set("kind", static_cast<int>(observation.kind));
  if (observation.root_kind) {
    value.Set("root_kind", static_cast<int>(*observation.root_kind));
  } else {
    value.Set("parent_id", observation.parent_id->AsLowercaseString());
  }
  value.Set("sort_key", observation.sort_key);
  value.Set("title", observation.title);
  value.Set("url", observation.url);
  serialization_internal::SetTime(value, "created_at", observation.created_at);
  return serialization_internal::WriteDict(value, payload);
}

std::optional<BookmarkRecord> DeserializeNativeBookmarkObservation(
    const std::string& payload) {
  auto value = serialization_internal::ParseDict(payload);
  BookmarkRecord result;
  if (!value || value->size() != 8 ||
      value->FindBool("native_observation") != true ||
      !serialization_internal::ReadUuid(*value, "id", &result.id,
                                        /*optional=*/false) ||
      !serialization_internal::ReadString(*value, "sort_key",
                                          &result.sort_key) ||
      !serialization_internal::ReadString(*value, "title", &result.title) ||
      !serialization_internal::ReadString(*value, "url", &result.url) ||
      !serialization_internal::ReadTime(*value, "created_at",
                                        &result.created_at)) {
    return std::nullopt;
  }
  const auto kind = value->FindInt("kind");
  if (!kind) {
    return std::nullopt;
  }
  result.kind = static_cast<BookmarkKind>(*kind);
  if (auto root = value->FindInt("root_kind")) {
    result.root_kind = static_cast<BookmarkRoot>(*root);
  } else {
    base::Uuid parent;
    if (!serialization_internal::ReadUuid(*value, "parent_id", &parent,
                                          /*optional=*/false)) {
      return std::nullopt;
    }
    result.parent_id = parent;
  }
  return ValidateNativeBookmarkShape(result) ? std::optional(result)
                                             : std::nullopt;
}

}  // namespace ahoi::sync
