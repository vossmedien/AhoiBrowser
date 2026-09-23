// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_BUNDLE_H_
#define AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_BUNDLE_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "ahoi/browser/session/portable_workspace_structure.h"

namespace ahoi::session {

inline constexpr int kPortableWorkspaceBundleVersion = 1;
inline constexpr size_t kMaximumPortableWorkspaceBundleBytes = 16 * 1024 * 1024;

// Encodes only a previously selected portable structure. Returns nullopt for
// malformed graphs, nonportable targets or excessive output; it never writes
// a file or reads native profile paths. The on-disk format is independent of
// Ahoi's Sync wire and SQLite persistence schemas.
std::optional<std::string> EncodePortableWorkspaceBundle(
    const PortableWorkspaceStructure& structure);

// Strictly decodes the single current file version into detached values.
// Parsing never grants import authority or changes an existing workspace;
// callers must still preview conflicts and commit through one transaction.
std::optional<PortableWorkspaceStructure> DecodePortableWorkspaceBundle(
    std::string_view bytes);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_PORTABLE_WORKSPACE_BUNDLE_H_
