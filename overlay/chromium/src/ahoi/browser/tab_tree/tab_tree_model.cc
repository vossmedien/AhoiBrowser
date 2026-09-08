// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/tab_tree/tab_tree_model.h"

#include <utility>

#include "ahoi/browser/tab_tree/shared_tab_target_policy.h"

namespace ahoi::tab_tree {

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
