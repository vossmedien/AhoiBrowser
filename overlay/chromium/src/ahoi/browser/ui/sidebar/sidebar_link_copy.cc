// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_link_copy.h"

#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"

namespace ahoi::sidebar {

namespace {

constexpr std::u16string_view kMarkdownAsciiPunctuation =
    uR"markdown(!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~)markdown";

std::u16string EscapeMarkdownLinkLabel(std::u16string_view title) {
  std::u16string escaped;
  escaped.reserve(title.size());
  bool last_was_space = false;
  for (const char16_t character : title) {
    if (character == u'\r' || character == u'\n' || character == u'\t') {
      if (!escaped.empty() && !last_was_space) {
        escaped.push_back(u' ');
        last_was_space = true;
      }
      continue;
    }
    // CommonMark permits a backslash to escape every ASCII punctuation
    // character. Escaping the complete set keeps an untrusted page title
    // literal instead of letting inline HTML, entities, emphasis or code
    // change the copied link's rendered markup.
    if (kMarkdownAsciiPunctuation.find(character) !=
        std::u16string_view::npos) {
      escaped.push_back(u'\\');
    }
    escaped.push_back(character);
    last_was_space = character == u' ';
  }
  return escaped;
}

std::u16string EscapeMarkdownLinkDestination(std::u16string_view url) {
  std::u16string escaped;
  escaped.reserve(url.size());
  for (const char16_t character : url) {
    if (character == u'\\' || character == u'(' || character == u')') {
      escaped.push_back(u'\\');
    }
    escaped.push_back(character);
  }
  return escaped;
}

}  // namespace

std::optional<std::u16string> BuildPageLinkClipboardText(
    const GURL& url,
    std::u16string_view title,
    PageLinkCopyFormat format) {
  if (!url.is_valid() || url.is_empty() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  GURL::Replacements replacements;
  replacements.ClearUsername();
  replacements.ClearPassword();
  const GURL safe_url = url.ReplaceComponents(replacements);
  if (!safe_url.is_valid() || safe_url.is_empty() ||
      !safe_url.SchemeIsHTTPOrHTTPS() || safe_url.has_username() ||
      safe_url.has_password()) {
    return std::nullopt;
  }

  const std::u16string url_text = base::UTF8ToUTF16(safe_url.spec());
  if (format == PageLinkCopyFormat::kUrl) {
    return url_text;
  }

  std::u16string label = EscapeMarkdownLinkLabel(title);
  if (label.empty()) {
    label = EscapeMarkdownLinkLabel(base::UTF8ToUTF16(safe_url.host()));
  }
  if (label.empty()) {
    label = url_text;
  }
  return base::StrCat(
      {u"[", label, u"](", EscapeMarkdownLinkDestination(url_text), u")"});
}

tab_tree::TabTreeStore::Result BuildOrderedLinkList(
    tab_tree::TabTreeStore* store,
    const base::Uuid& workspace_id,
    std::optional<base::Uuid> folder_id,
    std::u16string* links) {
  if (!store || !workspace_id.is_valid() || !links ||
      (folder_id.has_value() && !folder_id->is_valid())) {
    return tab_tree::TabTreeStore::Result::kInvalidArgument;
  }

  if (folder_id.has_value()) {
    tab_tree::TreeNode folder;
    const tab_tree::TabTreeStore::Result folder_result =
        store->GetNode(*folder_id, &folder);
    if (folder_result != tab_tree::TabTreeStore::Result::kOk) {
      return folder_result;
    }
    if (folder.tombstone || folder.workspace_id != workspace_id ||
        folder.type != tab_tree::TreeNodeType::kFolder) {
      return tab_tree::TabTreeStore::Result::kInvalidArgument;
    }
  }

  std::vector<tab_tree::TreeNode> children;
  const tab_tree::TabTreeStore::Result root_result =
      store->GetChildren(workspace_id, folder_id, &children);
  if (root_result != tab_tree::TabTreeStore::Result::kOk) {
    return root_result;
  }

  std::vector<tab_tree::TreeNode> pending;
  pending.reserve(children.size());
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    pending.push_back(std::move(*it));
  }

  std::unordered_set<base::Uuid, base::UuidHash> visited;
  std::u16string ordered_links;
  while (!pending.empty()) {
    tab_tree::TreeNode node = std::move(pending.back());
    pending.pop_back();
    if (!visited.insert(node.id).second) {
      return tab_tree::TabTreeStore::Result::kDatabaseError;
    }
    if (node.type == tab_tree::TreeNodeType::kSavedPage) {
      if (!node.url.is_valid() || node.url.is_empty()) {
        return tab_tree::TabTreeStore::Result::kDatabaseError;
      }
      if (!ordered_links.empty()) {
        ordered_links.push_back(u'\n');
      }
      ordered_links.append(base::UTF8ToUTF16(node.url.spec()));
      continue;
    }

    children.clear();
    const tab_tree::TabTreeStore::Result child_result =
        store->GetChildren(workspace_id, node.id, &children);
    if (child_result != tab_tree::TabTreeStore::Result::kOk) {
      return child_result;
    }
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      pending.push_back(std::move(*it));
    }
  }

  *links = std::move(ordered_links);
  return tab_tree::TabTreeStore::Result::kOk;
}

}  // namespace ahoi::sidebar
