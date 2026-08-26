// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_WORKSPACE_SESSION_METADATA_H_
#define AHOI_BROWSER_SESSION_WORKSPACE_SESSION_METADATA_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/uuid.h"

namespace ahoi::session {

// SessionService persists extra data as strings. A versioned payload keeps the
// Ahoi-specific schema independently evolvable and lets restore reject corrupt
// or future data without accepting a partially decoded workspace assignment.
inline constexpr int kWorkspaceSessionMetadataVersion = 1;
inline constexpr char kWindowSessionMetadataExtraDataKey[] =
    "ahoi.workspace_session.window";
inline constexpr char kTabSessionMetadataExtraDataKey[] =
    "ahoi.workspace_session.tab";

struct WindowSessionMetadata {
  base::Uuid active_workspace_id;

  bool operator==(const WindowSessionMetadata&) const = default;
};

struct TabSessionMetadata {
  base::Uuid workspace_id;
  std::optional<base::Uuid> tree_node_id;
  bool last_active_in_workspace = false;

  bool operator==(const TabSessionMetadata&) const = default;
};

enum class SessionMetadataDecodeResult {
  kSuccess = 0,
  kMalformed = 1,
  kUnsupportedVersion = 2,
};

// Encoding fails closed for invalid UUIDs. Decoding accepts only the complete
// current schema and never changes `metadata` unless it succeeds.
std::optional<std::string> EncodeWindowSessionMetadata(
    const WindowSessionMetadata& metadata);
SessionMetadataDecodeResult DecodeWindowSessionMetadata(
    std::string_view serialized,
    WindowSessionMetadata* metadata);

std::optional<std::string> EncodeTabSessionMetadata(
    const TabSessionMetadata& metadata);
SessionMetadataDecodeResult DecodeTabSessionMetadata(
    std::string_view serialized,
    TabSessionMetadata* metadata);

// Uses the requested workspace only when it is valid and still present.
// Otherwise the first valid workspace in display order is returned. Empty or
// wholly invalid snapshots deliberately yield nullopt rather than inventing an
// identity.
std::optional<base::Uuid> ResolveWorkspaceForRestore(
    const std::optional<base::Uuid>& requested_workspace,
    base::span<const base::Uuid> available_workspaces);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_WORKSPACE_SESSION_METADATA_H_
