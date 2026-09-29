// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_split_receipt.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/session/workspace_session_metadata.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/session_types.h"
#include "components/split_tabs/split_tab_id.h"
#include "crypto/hash.h"

namespace ahoi::importer::arc {

namespace {

constexpr double kRatioTotalTolerance = 0.001;

class FingerprintWriter {
 public:
  explicit FingerprintWriter(std::string_view domain)
      : hasher_(crypto::hash::kSha256) {
    WriteString(domain);
  }

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

  void WriteUuid(const base::Uuid& value) {
    WriteString(value.AsLowercaseString());
  }

  void WriteDouble(double value) {
    WriteUnsigned(std::bit_cast<uint64_t>(value));
  }

  void WriteVisualData(const split_tabs::SplitTabVisualData& visual_data) {
    WriteSigned(static_cast<int64_t>(visual_data.split_layout()));
    WriteDouble(visual_data.split_ratio());
    WriteSigned(static_cast<int64_t>(visual_data.arrangement()));
    WriteDouble(visual_data.secondary_split_ratio());
  }

  std::string Finish() {
    std::array<uint8_t, crypto::hash::kSha256Size> digest{};
    hasher_.Finish(digest);
    return base::HexEncodeLower(digest);
  }

 private:
  crypto::hash::Hasher hasher_;
};

using NodeMap = std::map<base::Uuid, const tab_tree::TreeNode*>;

bool BuildNodeMap(const ArcImportPlan& plan, NodeMap* nodes) {
  if (!nodes) {
    return false;
  }
  for (const tab_tree::TreeNode& node : plan.tree.nodes) {
    if (!node.id.is_valid() || !nodes->emplace(node.id, &node).second) {
      return false;
    }
  }
  return true;
}

struct DecodedSessionMember {
  raw_ptr<const sessions::SessionWindow> window = nullptr;
  raw_ptr<const sessions::SessionTab> tab = nullptr;
  size_t position = 0;
  session::TabSessionMetadata metadata;
};

using DecodedMemberMap = std::map<base::Uuid, DecodedSessionMember>;

bool MembersFollowSourceOrder(
    const std::vector<const DecodedSessionMember*>& members) {
  for (size_t index = 1; index < members.size(); ++index) {
    if (members[index - 1]->position >= members[index]->position ||
        members[index - 1]->tab->tab_visual_index >=
            members[index]->tab->tab_visual_index) {
      return false;
    }
  }
  return true;
}

ArcSplitReceipt FailReceipt(ArcSplitReceipt receipt,
                            ArcSplitVerification verification,
                            ArcSplitReceiptFailure failure) {
  receipt.verification = verification;
  receipt.failure = failure;
  receipt.verified_split_count = 0;
  receipt.focus_verified = false;
  receipt.receipt_sha256.clear();
  return receipt;
}

}  // namespace

std::optional<ArcSplitVisualExpectation> BuildArcSplitVisualExpectation(
    const ArcSplitDescriptor& split) {
  const size_t count = split.member_node_ids.size();
  if ((split.orientation != ArcSplitOrientation::kHorizontal &&
       split.orientation != ArcSplitOrientation::kVertical) ||
      count < 2u || count > split_tabs::SplitTabVisualData::kMaxPanes ||
      split.normalized_ratios.size() != count) {
    return std::nullopt;
  }
  double total = 0.0;
  for (double ratio : split.normalized_ratios) {
    if (!std::isfinite(ratio) || ratio <= 0.0) {
      return std::nullopt;
    }
    total += ratio;
  }
  if (!std::isfinite(total) || std::abs(total - 1.0) > kRatioTotalTolerance) {
    return std::nullopt;
  }

  const split_tabs::SplitTabLayout layout =
      split.orientation == ArcSplitOrientation::kHorizontal
          ? split_tabs::SplitTabLayout::kSideBySide
          : split_tabs::SplitTabLayout::kStacked;
  if (count == 2u) {
    split_tabs::SplitTabVisualData visual_data(layout);
    if (!visual_data.set_split_ratio(split.normalized_ratios[0])) {
      return std::nullopt;
    }
    return ArcSplitVisualExpectation{.visual_data = std::move(visual_data)};
  }
  if (count == 3u) {
    split_tabs::SplitTabVisualData visual_data =
        split_tabs::SplitTabVisualData::ForThreePane(
            layout, split_tabs::SplitTabArrangement::kLinear);
    const double remaining =
        split.normalized_ratios[1] + split.normalized_ratios[2];
    if (!visual_data.set_split_ratio(split.normalized_ratios[0]) ||
        !visual_data.set_secondary_split_ratio(split.normalized_ratios[1] /
                                               remaining)) {
      return std::nullopt;
    }
    return ArcSplitVisualExpectation{.visual_data = std::move(visual_data)};
  }

  // Chromium/Ahoi renders four panes as a 2x2 grid. Arc schema 1 can encode
  // four independent linear factors, so the runtime uses balanced grid ratios
  // while preserving orientation and source order.
  return ArcSplitVisualExpectation{
      .visual_data = split_tabs::SplitTabVisualData::ForFourPane(layout),
      .approximated_four_pane_ratios = true,
  };
}

bool IsValidArcSplitStructure(const ArcImportPlan& plan) {
  if (plan.schema_version != kArcImportPlanSchemaVersion ||
      plan.tree.nodes.size() > kMaxItemCount ||
      plan.splits.size() > kMaxItemCount) {
    return false;
  }
  NodeMap nodes;
  if (!BuildNodeMap(plan, &nodes)) {
    return false;
  }

  std::set<base::Uuid> claimed_folders;
  std::set<base::Uuid> claimed_members;
  for (const ArcSplitDescriptor& split : plan.splits) {
    if (!split.folder_node_id.is_valid() ||
        !split.focused_member_node_id.is_valid() ||
        !claimed_folders.insert(split.folder_node_id).second ||
        !BuildArcSplitVisualExpectation(split).has_value() ||
        std::ranges::find(split.member_node_ids,
                          split.focused_member_node_id) ==
            split.member_node_ids.end()) {
      return false;
    }

    const auto folder_it = nodes.find(split.folder_node_id);
    if (folder_it == nodes.end() || folder_it->second->tombstone ||
        !folder_it->second->workspace_id.is_valid() ||
        folder_it->second->type != tab_tree::TreeNodeType::kFolder) {
      return false;
    }
    for (const base::Uuid& member_id : split.member_node_ids) {
      const auto member_it = nodes.find(member_id);
      if (!member_id.is_valid() || member_it == nodes.end() ||
          member_it->second->tombstone ||
          member_it->second->type != tab_tree::TreeNodeType::kSavedPage ||
          member_it->second->workspace_id != folder_it->second->workspace_id ||
          member_it->second->parent_id != split.folder_node_id ||
          !claimed_members.insert(member_id).second) {
        return false;
      }
    }
  }
  return true;
}

std::string ComputeArcSplitStructureFingerprint(const ArcImportPlan& plan) {
  if (!IsValidArcSplitStructure(plan)) {
    return std::string();
  }

  NodeMap nodes;
  if (!BuildNodeMap(plan, &nodes)) {
    return std::string();
  }
  FingerprintWriter writer("ahoi-arc-split-structure-v1");
  writer.WriteSigned(plan.schema_version);
  writer.WriteUnsigned(plan.splits.size());
  for (const ArcSplitDescriptor& split : plan.splits) {
    const ArcSplitVisualExpectation visual =
        *BuildArcSplitVisualExpectation(split);
    const tab_tree::TreeNode* const folder = nodes.at(split.folder_node_id);
    writer.WriteString("split");
    writer.WriteUuid(split.folder_node_id);
    writer.WriteUuid(folder->workspace_id);
    writer.WriteSigned(static_cast<int64_t>(split.orientation));
    writer.WriteUuid(split.focused_member_node_id);
    writer.WriteUnsigned(split.member_node_ids.size());
    for (size_t index = 0; index < split.member_node_ids.size(); ++index) {
      const base::Uuid& member_id = split.member_node_ids[index];
      writer.WriteUuid(member_id);
      writer.WriteUuid(nodes.at(member_id)->workspace_id);
      writer.WriteDouble(split.normalized_ratios[index]);
    }
    writer.WriteVisualData(visual.visual_data);
    writer.WriteBool(visual.approximated_four_pane_ratios);
  }
  return writer.Finish();
}

ArcSplitReceipt VerifyArcSplitSessionWindows(
    const ArcImportPlan& plan,
    SessionID target_window_id,
    const std::vector<std::unique_ptr<sessions::SessionWindow>>& windows,
    SessionID /*active_window_id*/,
    bool require_focus) {
  ArcSplitReceipt receipt;
  receipt.structure_sha256 = ComputeArcSplitStructureFingerprint(plan);
  if (receipt.structure_sha256.empty()) {
    return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                       ArcSplitReceiptFailure::kInvalidStructure);
  }
  if (!target_window_id.is_valid()) {
    return FailReceipt(std::move(receipt), ArcSplitVerification::kUnavailable,
                       ArcSplitReceiptFailure::kInvalidTargetWindowId);
  }

  NodeMap nodes;
  if (!BuildNodeMap(plan, &nodes)) {
    return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                       ArcSplitReceiptFailure::kInvalidStructure);
  }
  std::set<base::Uuid> expected_member_ids;
  for (const ArcSplitDescriptor& split : plan.splits) {
    expected_member_ids.insert(split.member_node_ids.begin(),
                               split.member_node_ids.end());
  }

  const sessions::SessionWindow* target_window = nullptr;
  DecodedMemberMap decoded_members;
  for (const std::unique_ptr<sessions::SessionWindow>& window : windows) {
    if (!window) {
      continue;
    }
    if (window->window_id == target_window_id) {
      if (target_window) {
        return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                           ArcSplitReceiptFailure::kDuplicateTargetWindow);
      }
      target_window = window.get();
    }
    for (size_t position = 0; position < window->tabs.size(); ++position) {
      const sessions::SessionTab* const tab = window->tabs[position].get();
      if (!tab) {
        continue;
      }
      const auto serialized =
          tab->extra_data.find(session::kTabSessionMetadataExtraDataKey);
      if (serialized == tab->extra_data.end()) {
        continue;
      }
      session::TabSessionMetadata metadata;
      if (session::DecodeTabSessionMetadata(serialized->second, &metadata) !=
              session::SessionMetadataDecodeResult::kSuccess ||
          !metadata.tree_node_id.has_value() ||
          !expected_member_ids.contains(*metadata.tree_node_id)) {
        continue;
      }
      // Preserve the key before moving `metadata` into the map value. Function
      // argument evaluation order must not allow the move to invalidate the
      // UUID that is also used as the map key.
      const base::Uuid tree_node_id = *metadata.tree_node_id;
      if (!decoded_members
               .emplace(tree_node_id,
                        DecodedSessionMember{.window = window.get(),
                                             .tab = tab,
                                             .position = position,
                                             .metadata = std::move(metadata)})
               .second) {
        return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                           ArcSplitReceiptFailure::kDuplicateMemberMetadata);
      }
    }
  }
  if (!target_window ||
      target_window->type != sessions::SessionWindow::TYPE_NORMAL) {
    return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                       ArcSplitReceiptFailure::kInvalidTargetWindow);
  }

  std::set<SessionID> claimed_session_tab_ids;
  std::set<split_tabs::SplitTabId> claimed_split_ids;
  std::map<base::Uuid, const DecodedSessionMember*> verified_members;
  FingerprintWriter receipt_writer("ahoi-arc-split-session-receipt-v1");
  receipt_writer.WriteString(receipt.structure_sha256);
  receipt_writer.WriteSigned(target_window_id.id());
  receipt_writer.WriteUnsigned(plan.splits.size());

  for (const ArcSplitDescriptor& split : plan.splits) {
    const ArcSplitVisualExpectation visual =
        *BuildArcSplitVisualExpectation(split);
    std::vector<const DecodedSessionMember*> members;
    for (const base::Uuid& member_id : split.member_node_ids) {
      const auto decoded_it = decoded_members.find(member_id);
      if (decoded_it == decoded_members.end() ||
          decoded_it->second.window != target_window ||
          decoded_it->second.tab->window_id != target_window_id ||
          !decoded_it->second.tab->tab_id.is_valid() ||
          decoded_it->second.tab->tab_visual_index < 0 ||
          decoded_it->second.metadata.workspace_id !=
              nodes.at(member_id)->workspace_id ||
          !claimed_session_tab_ids.insert(decoded_it->second.tab->tab_id)
               .second) {
        return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                           ArcSplitReceiptFailure::kInvalidMember);
      }
      members.push_back(&decoded_it->second);
      verified_members.emplace(member_id, &decoded_it->second);
    }
    if (!MembersFollowSourceOrder(members)) {
      return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                         ArcSplitReceiptFailure::kMemberOrderMismatch);
    }

    const std::optional<split_tabs::SplitTabId> split_id =
        members.front()->tab->split_id;
    if (!split_id.has_value() || split_id->is_empty() ||
        !claimed_split_ids.insert(*split_id).second ||
        !std::ranges::all_of(members, [&](const DecodedSessionMember* member) {
          return member->tab->split_id == split_id;
        })) {
      return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                         ArcSplitReceiptFailure::kInvalidSplitMembership);
    }

    size_t observed_member_count = 0;
    for (const std::unique_ptr<sessions::SessionWindow>& window : windows) {
      if (!window) {
        continue;
      }
      for (const std::unique_ptr<sessions::SessionTab>& tab : window->tabs) {
        if (!tab || tab->split_id != split_id) {
          continue;
        }
        ++observed_member_count;
        if (window.get() != target_window ||
            std::ranges::find(members, tab.get(), &DecodedSessionMember::tab) ==
                members.end()) {
          return FailReceipt(
              std::move(receipt), ArcSplitVerification::kConflict,
              ArcSplitReceiptFailure::kUnexpectedSplitMembership);
        }
      }
    }
    if (observed_member_count != members.size()) {
      return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                         ArcSplitReceiptFailure::kUnexpectedSplitMembership);
    }

    const sessions::SessionSplitTab* matching_split = nullptr;
    size_t matching_split_count = 0;
    for (const std::unique_ptr<sessions::SessionWindow>& window : windows) {
      if (!window) {
        continue;
      }
      for (const std::unique_ptr<sessions::SessionSplitTab>& candidate :
           window->split_tabs) {
        if (!candidate || candidate->id_ != *split_id) {
          continue;
        }
        ++matching_split_count;
        if (window.get() != target_window) {
          return FailReceipt(std::move(receipt),
                             ArcSplitVerification::kConflict,
                             ArcSplitReceiptFailure::kInvalidSplitVisualRecord);
        }
        matching_split = candidate.get();
      }
    }
    if (matching_split_count != 1u || !matching_split ||
        matching_split->split_visual_data_ != visual.visual_data) {
      return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                         ArcSplitReceiptFailure::kInvalidSplitVisualRecord);
    }

    receipt_writer.WriteString("split");
    receipt_writer.WriteUuid(split.folder_node_id);
    receipt_writer.WriteUnsigned(members.size());
    for (size_t index = 0; index < members.size(); ++index) {
      receipt_writer.WriteUuid(split.member_node_ids[index]);
      receipt_writer.WriteSigned(members[index]->tab->tab_id.id());
      receipt_writer.WriteUnsigned(members[index]->position);
      receipt_writer.WriteSigned(members[index]->tab->tab_visual_index);
    }
    receipt_writer.WriteVisualData(matching_split->split_visual_data_);
    ++receipt.verified_split_count;
  }

  receipt_writer.WriteBool(require_focus);
  if (require_focus) {
    // Focus belongs to the selected member inside the import's target window.
    // The user may activate a different window while the asynchronous native
    // receipt is written; importing must neither steal that focus nor fail.
    if (plan.splits.empty() ||
        target_window->selected_tab_index < 0 ||
        static_cast<size_t>(target_window->selected_tab_index) >=
            target_window->tabs.size()) {
      return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                         ArcSplitReceiptFailure::kInvalidFocusContext);
    }
    const base::Uuid& expected_focus =
        plan.splits.back().focused_member_node_id;
    const auto focused_it = verified_members.find(expected_focus);
    const sessions::SessionTab* const selected_tab =
        target_window->tabs[target_window->selected_tab_index].get();
    if (focused_it == verified_members.end() || !selected_tab ||
        selected_tab != focused_it->second->tab) {
      return FailReceipt(std::move(receipt), ArcSplitVerification::kConflict,
                         ArcSplitReceiptFailure::kFocusMismatch);
    }
    receipt.focus_verified = true;
    receipt_writer.WriteUuid(expected_focus);
    receipt_writer.WriteSigned(selected_tab->tab_id.id());
  }

  receipt.verification = ArcSplitVerification::kExact;
  receipt.failure = ArcSplitReceiptFailure::kNone;
  receipt.receipt_sha256 = receipt_writer.Finish();
  return receipt;
}

}  // namespace ahoi::importer::arc
