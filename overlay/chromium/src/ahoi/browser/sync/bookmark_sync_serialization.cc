// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/bookmark_sync_serialization.h"

#include <utility>

#include "ahoi/browser/sync/sync_serialization_internal.h"

namespace ahoi::sync::serialization_internal {
namespace {

bool HasValidShape(const BookmarkRecord& record) {
  if (record.model_version != kBookmarkWireModelVersion ||
      record.version.model_version != record.model_version ||
      (record.kind != BookmarkKind::kFolder &&
       record.kind != BookmarkKind::kUrl) ||
      record.root_kind.has_value() == record.parent_id.has_value()) {
    return false;
  }
  if (record.parent_id) {
    return record.parent_id->is_valid();
  }
  return *record.root_kind == BookmarkRoot::kBookmarkBar ||
         *record.root_kind == BookmarkRoot::kOther ||
         *record.root_kind == BookmarkRoot::kMobile;
}

}  // namespace

bool SerializeBookmark(const BookmarkRecord& record, std::string* payload) {
  if (!payload || !HasValidShape(record)) {
    return false;
  }
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("kind", static_cast<int>(record.kind));
  if (record.root_kind) {
    dict.Set("root_kind", static_cast<int>(*record.root_kind));
  } else {
    dict.Set("parent_id", record.parent_id->AsLowercaseString());
  }
  dict.Set("sort_key", record.sort_key);
  dict.Set("title", record.title);
  dict.Set("url", record.url);
  SetTime(dict, "created_at", record.created_at);
  return WriteDict(dict, payload);
}

bool DeserializeBookmark(const Dict& dict, BookmarkRecord* record) {
  if (!record) {
    return false;
  }
  BookmarkRecord decoded;
  if (!ReadCommon(dict, &decoded.model_version, &decoded.id, &decoded.tombstone,
                  &decoded.version, &decoded.field_versions) ||
      !ReadString(dict, "sort_key", &decoded.sort_key) ||
      !ReadString(dict, "title", &decoded.title) ||
      !ReadString(dict, "url", &decoded.url) ||
      !ReadTime(dict, "created_at", &decoded.created_at)) {
    return false;
  }
  const std::optional<int> kind = dict.FindInt("kind");
  if (!kind || *kind < static_cast<int>(BookmarkKind::kFolder) ||
      *kind > static_cast<int>(BookmarkKind::kUrl)) {
    return false;
  }
  decoded.kind = static_cast<BookmarkKind>(*kind);

  // A present null, stringified enum or non-string UUID is malformed, not an
  // absent optional field. Top-level and nested locations must stay distinct.
  const base::Value* root = dict.Find("root_kind");
  const base::Value* parent = dict.Find("parent_id");
  if ((root != nullptr) == (parent != nullptr)) {
    return false;
  }
  if (root) {
    if (!root->is_int() ||
        root->GetInt() < static_cast<int>(BookmarkRoot::kBookmarkBar) ||
        root->GetInt() > static_cast<int>(BookmarkRoot::kMobile)) {
      return false;
    }
    decoded.root_kind = static_cast<BookmarkRoot>(root->GetInt());
  } else {
    base::Uuid parent_id;
    if (!ReadUuid(dict, "parent_id", &parent_id, /*optional=*/false)) {
      return false;
    }
    decoded.parent_id = parent_id;
  }
  if (!HasValidShape(decoded)) {
    return false;
  }
  *record = std::move(decoded);
  return true;
}

}  // namespace ahoi::sync::serialization_internal
