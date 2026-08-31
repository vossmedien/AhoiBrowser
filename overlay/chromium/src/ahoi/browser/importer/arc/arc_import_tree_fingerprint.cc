// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_tree_fingerprint.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/hash.h"

namespace ahoi::importer::arc {

namespace {

class FingerprintWriter {
 public:
  FingerprintWriter() : hasher_(crypto::hash::kSha256) {
    WriteString("ahoi-arc-tab-tree-fingerprint-v1");
  }

  void WriteTag(std::string_view value) { WriteString(value); }

  void WriteBool(bool value) { WriteUnsigned(value ? 1u : 0u); }

  void WriteSigned(int64_t value) {
    WriteUnsigned(static_cast<uint64_t>(value));
  }

  void WriteUnsigned(uint64_t value) {
    std::array<uint8_t, sizeof(value)> bytes{};
    for (size_t index = 0; index < bytes.size(); ++index) {
      bytes[index] =
          static_cast<uint8_t>(value >> ((bytes.size() - index - 1) * 8));
    }
    hasher_.Update(bytes);
  }

  void WriteString(std::string_view value) {
    WriteUnsigned(value.size());
    hasher_.Update(value);
  }

  void WriteString16(std::u16string_view value) {
    WriteUnsigned(value.size());
    for (char16_t code_unit : value) {
      const std::array<uint8_t, 2> bytes = {
          static_cast<uint8_t>(code_unit >> 8),
          static_cast<uint8_t>(code_unit & 0xff)};
      hasher_.Update(bytes);
    }
  }

  void WriteUuid(const base::Uuid& value) {
    WriteString(value.AsLowercaseString());
  }

  void WriteTime(base::Time value) {
    WriteSigned(value.ToDeltaSinceWindowsEpoch().InMicroseconds());
  }

  void WriteOptionalUuid(const std::optional<base::Uuid>& value) {
    WriteBool(value.has_value());
    if (value) {
      WriteUuid(*value);
    }
  }

  void WriteOptionalUnsigned(const std::optional<uint32_t>& value) {
    WriteBool(value.has_value());
    if (value) {
      WriteUnsigned(*value);
    }
  }

  std::string Finish() {
    std::array<uint8_t, crypto::hash::kSha256Size> digest{};
    hasher_.Finish(digest);
    return base::HexEncodeLower(digest);
  }

 private:
  crypto::hash::Hasher hasher_;
};

void WriteWorkspace(FingerprintWriter* writer,
                    const tab_tree::Workspace& workspace) {
  writer->WriteTag("workspace");
  writer->WriteSigned(workspace.model_version);
  writer->WriteUuid(workspace.id);
  writer->WriteString16(workspace.name);
  writer->WriteString16(workspace.icon);
  writer->WriteString(workspace.sort_key);
  writer->WriteOptionalUnsigned(workspace.accent_argb);
  writer->WriteTime(workspace.created_at);
  writer->WriteTime(workspace.modified_at);
  writer->WriteBool(workspace.tombstone);
}

void WriteTreeNode(FingerprintWriter* writer, const tab_tree::TreeNode& node) {
  writer->WriteTag("tree-node");
  writer->WriteSigned(node.model_version);
  writer->WriteUuid(node.id);
  writer->WriteUuid(node.workspace_id);
  writer->WriteOptionalUuid(node.parent_id);
  writer->WriteSigned(static_cast<int64_t>(node.type));
  writer->WriteString16(node.title);
  writer->WriteString16(node.icon);
  writer->WriteOptionalUnsigned(node.accent_argb);
  writer->WriteString(node.url.spec());
  writer->WriteString(node.sort_key);
  writer->WriteTime(node.created_at);
  writer->WriteTime(node.modified_at);
  writer->WriteBool(node.tombstone);
}

void WriteUndoOperation(FingerprintWriter* writer,
                        const tab_tree::UndoOperationSnapshot& operation) {
  writer->WriteTag("undo-operation");
  writer->WriteSigned(operation.operation_id);
  writer->WriteSigned(static_cast<int64_t>(operation.kind));
  writer->WriteUuid(operation.subject_node_id);
  writer->WriteTime(operation.created_at);
  writer->WriteUnsigned(operation.nodes.size());
  for (const tab_tree::UndoNodeSnapshot& node : operation.nodes) {
    // Inner ordering is the persisted undo ordinal and is intentionally kept.
    writer->WriteTag("undo-node");
    writer->WriteUuid(node.node_id);
    writer->WriteBool(node.previous.has_value());
    if (node.previous) {
      WriteTreeNode(writer, *node.previous);
    }
  }
}

}  // namespace

std::string ComputeArcImportTreeFingerprint(
    const tab_tree::TabTreeSnapshot& snapshot) {
  std::vector<tab_tree::Workspace> workspaces = snapshot.workspaces;
  std::ranges::sort(workspaces, [](const auto& left, const auto& right) {
    return left.id.AsLowercaseString() < right.id.AsLowercaseString();
  });
  std::vector<tab_tree::TreeNode> nodes = snapshot.nodes;
  std::ranges::sort(nodes, [](const auto& left, const auto& right) {
    return left.id.AsLowercaseString() < right.id.AsLowercaseString();
  });
  std::vector<tab_tree::UndoOperationSnapshot> undo_operations =
      snapshot.undo_operations;
  std::ranges::stable_sort(undo_operations,
                           [](const auto& left, const auto& right) {
                             return left.operation_id < right.operation_id;
                           });

  FingerprintWriter writer;
  writer.WriteTag("workspaces");
  writer.WriteUnsigned(workspaces.size());
  for (const tab_tree::Workspace& workspace : workspaces) {
    WriteWorkspace(&writer, workspace);
  }
  writer.WriteTag("nodes");
  writer.WriteUnsigned(nodes.size());
  for (const tab_tree::TreeNode& node : nodes) {
    WriteTreeNode(&writer, node);
  }
  writer.WriteTag("undo-operations");
  writer.WriteUnsigned(undo_operations.size());
  for (const tab_tree::UndoOperationSnapshot& operation : undo_operations) {
    WriteUndoOperation(&writer, operation);
  }
  return writer.Finish();
}

bool IsArcImportTreeFingerprint(std::string_view value) {
  return value.size() == 64 && std::ranges::all_of(value, [](char character) {
           return base::IsHexDigit(character) &&
                  (base::IsAsciiDigit(character) ||
                   base::IsAsciiLower(character));
         });
}

}  // namespace ahoi::importer::arc
