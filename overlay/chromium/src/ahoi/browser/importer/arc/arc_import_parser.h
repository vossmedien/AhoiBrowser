// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PARSER_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PARSER_H_

#include <string_view>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/uuid.h"

namespace ahoi::importer::arc {

// Produces an RFC 4122 version-5-shaped UUID from an Arc identifier and a
// type-specific domain. The input never leaves this component or appears in
// diagnostics.
base::Uuid MakeDeterministicArcId(std::string_view domain,
                                  std::string_view source_identifier);

// Parses only the documented Arc v1 StorableSidebar structure. The returned
// plan contains no live-profile mutation and is safe to preview before a later
// explicit, atomic merge.
ArcParseResult ParseArcSnapshot(const ArcImportSnapshot& snapshot);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PARSER_H_
