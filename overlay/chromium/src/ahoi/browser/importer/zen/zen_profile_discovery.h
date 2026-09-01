// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ZEN_ZEN_PROFILE_DISCOVERY_H_
#define AHOI_BROWSER_IMPORTER_ZEN_ZEN_PROFILE_DISCOVERY_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "ahoi/browser/importer/zen/zen_application_discovery.h"
#include "base/files/file_path.h"
#include "components/user_data_importer/common/importer_data_types.h"

namespace ahoi::importer::zen {

inline constexpr char kZenSessionStoreName[] = "zen-sessions.jsonlz4";
inline constexpr char kValidatedZenUpstreamRevision[] =
    "e89bd7796e2dcecaf0c483a795225ed9ec549bbd";

// A detected session store is deliberately not equivalent to an enabled
// structure importer. The current preparation recognizes the bounded container
// format while leaving mutation disabled until a versioned schema adapter and
// real-profile acceptance pass exist.
enum class ZenStructureCapability {
  kNotPresent,
  kMozLz4Candidate,
  kUnsupportedHeader,
  kUnsafeOrOversized,
};

struct ZenProfileDetail {
  base::FilePath path;
  std::u16string name;
  uint16_t services_supported = user_data_importer::NONE;
  ZenStructureCapability structure_capability =
      ZenStructureCapability::kNotPresent;
};

struct ZenSourceProfileMetadata {
  ZenStructureCapability structure_capability =
      ZenStructureCapability::kNotPresent;
};

struct ZenSourceProfilesResult {
  ZenImportAvailability availability = ZenImportAvailability::kNotInstalled;
  // Index-aligned only with the Zen profiles appended by the corresponding
  // discovery call, never with profiles already present in the destination.
  std::vector<ZenSourceProfileMetadata> metadata;
};

// Discovers regular Zen profiles under |zen_data_root|. The INI and every
// profile must remain below that root and may not traverse a symbolic link.
// The function performs bounded blocking I/O and must run off the UI thread.
std::vector<ZenProfileDetail> DiscoverZenProfilesAtRoot(
    const base::FilePath& zen_data_root);

// Adds an installed, non-running Zen bundle as a normal Firefox-compatible
// source to Chromium's existing import dialog. Only categories backed by
// actual, safe profile files are advertised; passwords remain excluded on
// macOS. Zen sidebar structure is not advertised by this seam.
ZenImportAvailability AppendZenSourceProfiles(
    const std::string& locale,
    std::vector<user_data_importer::SourceProfile>* profiles);

// Metadata-preserving variant used by ImporterList. Structure detection stays
// read-only and disabled; the result exists solely so Settings can explain why
// Zen structure is unavailable without pretending that it can be imported.
ZenSourceProfilesResult AppendZenSourceProfilesWithMetadata(
    const std::string& locale,
    std::vector<user_data_importer::SourceProfile>* profiles);

// Root-injected variant used by deterministic fixtures and embedder-owned
// discovery callers. It has the same no-mutation and category constraints as
// AppendZenSourceProfiles().
void AppendZenSourceProfilesAtRoot(
    const base::FilePath& zen_data_root,
    const std::string& locale,
    std::vector<user_data_importer::SourceProfile>* profiles);

namespace internal {

// Application- and root-injected seam for deterministic availability tests.
// Unavailable applications never append a source profile.
ZenImportAvailability AppendZenSourceProfilesForApplication(
    const ZenApplicationState& application,
    const base::FilePath& zen_data_root,
    const std::string& locale,
    std::vector<user_data_importer::SourceProfile>* profiles);

ZenSourceProfilesResult AppendZenSourceProfilesForApplicationWithMetadata(
    const ZenApplicationState& application,
    const base::FilePath& zen_data_root,
    const std::string& locale,
    std::vector<user_data_importer::SourceProfile>* profiles);

}  // namespace internal

}  // namespace ahoi::importer::zen

#endif  // AHOI_BROWSER_IMPORTER_ZEN_ZEN_PROFILE_DISCOVERY_H_
