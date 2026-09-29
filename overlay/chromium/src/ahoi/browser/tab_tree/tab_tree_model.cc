// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/tab_tree/tab_tree_model.h"

#include <ostream>
#include <utility>

#include "ahoi/browser/tab_tree/shared_tab_target_policy.h"
#include "base/strings/utf_string_conversions.h"

namespace ahoi::tab_tree {

namespace {

template <typename T>
void PrintOptionalInt(std::ostream* os, const std::optional<T>& value) {
  if (value) {
    *os << static_cast<int64_t>(*value);
  } else {
    *os << "none";
  }
}

}  // namespace

void PrintTo(const TreeNode& node, std::ostream* os) {
  *os << "{v=" << node.model_version << " id=" << node.id
      << " ws=" << node.workspace_id << " parent="
      << (node.parent_id ? node.parent_id->AsLowercaseString() : "none")
      << " type=" << static_cast<int>(node.type)
      << " title=" << base::UTF16ToUTF8(node.title)
      << " icon=" << base::UTF16ToUTF8(node.icon) << " accent=";
  PrintOptionalInt(os, node.accent_argb);
  *os << " url=" << node.url.possibly_invalid_spec()
      << " sort=" << node.sort_key << " created=" << node.created_at
      << " modified=" << node.modified_at << " tomb=" << node.tombstone
      << " temp=" << node.is_temporary << " target=";
  PrintOptionalInt(os, node.target_kind);
  *os << " scheme=" << node.local_scheme.value_or("none")
      << " home=" << node.home_url.possibly_invalid_spec() << " home_target=";
  PrintOptionalInt(os, node.home_target_kind);
  *os << " home_scheme=" << node.home_local_scheme.value_or("none") << "}";
}

std::optional<sync::SharedTabTarget> GetSharedPageTarget(const TreeNode& node) {
  if (node.type != TreeNodeType::kSavedPage ||
      (!node.url.is_empty() && !node.url.is_valid())) {
    return std::nullopt;
  }
  if (!node.target_kind) {
    if (node.local_scheme) {
      return std::nullopt;
    }
    return DescribeNativeSharedTabTarget(node.url,
                                         NativeSharedTabParticipation::kNormal);
  }
  SharedTabTarget target{.kind = *node.target_kind,
                         .url = *node.target_kind == SharedTabTargetKind::kWeb
                                    ? node.url.spec()
                                    : std::string(),
                         .local_scheme = node.local_scheme};
  return IsValidSharedPageTarget(target, node.is_temporary)
             ? std::make_optional(std::move(target))
             : std::nullopt;
}

std::optional<sync::SharedTabTarget> GetSharedHomeTarget(const TreeNode& node) {
  if (node.type != TreeNodeType::kSavedPage ||
      (!node.home_url.is_empty() && !node.home_url.is_valid())) {
    return std::nullopt;
  }
  if (!node.home_target_kind) {
    return node.home_local_scheme
               ? std::nullopt
               : DescribeNativeSharedTabTarget(
                     node.home_url, NativeSharedTabParticipation::kNormal);
  }
  SharedTabTarget target{
      .kind = *node.home_target_kind,
      .url = *node.home_target_kind == SharedTabTargetKind::kWeb
                 ? node.home_url.spec()
                 : std::string(),
      .local_scheme = node.home_local_scheme};
  return IsValidSharedPageTarget(target, /*is_temporary=*/false)
             ? std::make_optional(std::move(target))
             : std::nullopt;
}

void InitializeSavedHome(TreeNode* node) {
  if (!node || node->type != TreeNodeType::kSavedPage || node->is_temporary ||
      !node->home_url.is_empty() || node->home_target_kind ||
      node->home_local_scheme || node->url.is_empty() ||
      !node->url.is_valid()) {
    return;
  }
  const auto target = DescribeNativeSharedTabTarget(
      node->url, NativeSharedTabParticipation::kNormal);
  if (!target || target->kind == SharedTabTargetKind::kNewTab) {
    return;
  }
  node->home_url = node->url;
  node->home_target_kind = target->kind;
  node->home_local_scheme = target->local_scheme;
}

std::u16string GetSharedPageTitle(const TreeNode& node) {
  const auto target = GetSharedPageTarget(node);
  if (target && target->kind == SharedTabTargetKind::kLocalOnly) {
    return u"Local page";
  }
  if (target && target->kind == SharedTabTargetKind::kNewTab) {
    return u"New tab";
  }
  return node.title;
}

}  // namespace ahoi::tab_tree
