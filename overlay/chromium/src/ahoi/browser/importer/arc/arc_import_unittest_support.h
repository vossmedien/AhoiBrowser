// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_UNITTEST_SUPPORT_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_UNITTEST_SUPPORT_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/importer/arc/arc_import_snapshot.h"
#include "crypto/hash.h"

// Synthetic Arc sidebar fixture and helpers shared by the Arc import unit
// tests (split from arc_import_unittest.cc, source line budget).
namespace ahoi::importer::arc::test_support {

inline constexpr char kValidArcSidebar[] = R"json({
  "version": 1,
  "sidebarSyncState": {
    "container": {
      "value": {
        "version": 6,
        "orderedSpaceIDs": ["space-a"]
      }
    },
    "spaceModels": [
      "space-a",
      {
        "value": {
          "id": "space-a",
          "title": "Work",
          "containerIDs": [],
          "newContainerIDs": [
            {"unpinned": {"_0": {"shared": {}}}},
            "root-unpinned",
            {"pinned": {}},
            "root-pinned"
          ]
        }
      }
    ],
    "items": [
      "root-pinned",
      {
        "value": {
          "id": "root-pinned",
          "parentID": null,
          "childrenIds": ["tab-a"],
          "title": null,
          "data": {
            "itemContainer": {
              "containerType": {"spaceItems": {"_0": "space-a"}}
            }
          }
        }
      },
      "root-unpinned",
      {
        "value": {
          "id": "root-unpinned",
          "parentID": null,
          "childrenIds": [
            "folder-a", "split-a", "unsafe-file", "unsafe-creds",
            "unsupported-a"
          ],
          "title": null,
          "data": {
            "itemContainer": {
              "containerType": {"spaceItems": {"_0": "space-a"}}
            }
          }
        }
      },
      "topapps-root",
      {
        "value": {
          "id": "topapps-root",
          "parentID": null,
          "childrenIds": [],
          "title": null,
          "data": {
            "itemContainer": {
              "containerType": {"topApps": {"_0": {}}}
            }
          }
        }
      },
      "tab-a",
      {
        "value": {
          "id": "tab-a",
          "parentID": "root-pinned",
          "childrenIds": [],
          "title": null,
          "data": {
            "tab": {
              "savedTitle": "Pinned page",
              "savedURL": "https://pinned.example.test/path"
            }
          }
        }
      },
      "folder-a",
      {
        "value": {
          "id": "folder-a",
          "parentID": "root-unpinned",
          "childrenIds": ["tab-b"],
          "title": "Folder",
          "data": {"list": {}}
        }
      },
      "tab-b",
      {
        "value": {
          "id": "tab-b",
          "parentID": "folder-a",
          "childrenIds": [],
          "title": "Nested page",
          "data": {
            "tab": {
              "savedTitle": "Nested page fallback",
              "savedURL": "https://nested.example.test/"
            }
          }
        }
      },
      "split-a",
      {
        "value": {
          "id": "split-a",
          "parentID": "root-unpinned",
          "childrenIds": ["split-tab-a", "split-tab-b"],
          "title": null,
          "data": {
            "splitView": {
              "layoutOrientation": "horizontal",
              "focusItemID": "split-tab-b",
              "itemWidthFactors": [
                "split-tab-a", 0.5,
                "split-tab-b", 0.5
              ],
              "customInfo": null,
              "timeLastActiveAt": null
            }
          }
        }
      },
      "split-tab-a",
      {
        "value": {
          "id": "split-tab-a",
          "parentID": "split-a",
          "childrenIds": [],
          "title": null,
          "data": {
            "tab": {
              "savedTitle": "Left",
              "savedURL": "https://left.example.test/"
            }
          }
        }
      },
      "split-tab-b",
      {
        "value": {
          "id": "split-tab-b",
          "parentID": "split-a",
          "childrenIds": [],
          "title": null,
          "data": {
            "tab": {
              "savedTitle": "Right",
              "savedURL": "https://right.example.test/"
            }
          }
        }
      },
      "unsafe-file",
      {
        "value": {
          "id": "unsafe-file",
          "parentID": "root-unpinned",
          "childrenIds": [],
          "title": "Local file",
          "data": {
            "tab": {
              "savedTitle": "Local file",
              "savedURL": "file:///private/example.txt"
            }
          }
        }
      },
      "unsafe-creds",
      {
        "value": {
          "id": "unsafe-creds",
          "parentID": "root-unpinned",
          "childrenIds": [],
          "title": "Credential URL",
          "data": {
            "tab": {
              "savedTitle": "Credential URL",
              "savedURL": "https://user:password@example.test/"
            }
          }
        }
      },
      "unsupported-a",
      {
        "value": {
          "id": "unsupported-a",
          "parentID": "root-unpinned",
          "childrenIds": [],
          "title": "Unsupported",
          "data": {"easel": {}}
        }
      }
    ]
  }
})json";

inline ArcImportSnapshot SnapshotFor(std::string json) {
  ArcImportSnapshot snapshot;
  snapshot.source_size = static_cast<int64_t>(json.size());
  snapshot.sha256 = crypto::hash::Sha256(json);
  snapshot.json = std::move(json);
  return snapshot;
}

inline bool ReplaceOnce(std::string* value,
                 std::string_view needle,
                 std::string_view replacement) {
  const size_t offset = value->find(needle);
  if (offset == std::string::npos) {
    return false;
  }
  value->replace(offset, needle.size(), replacement);
  return true;
}

}  // namespace ahoi::importer::arc::test_support

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_UNITTEST_SUPPORT_H_
