// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_UNITTEST_SUPPORT_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_UNITTEST_SUPPORT_H_

#include <string>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "sql/statement.h"

// Synthetic Chromium-format History fixtures for the Arc history tests. The
// rows are invented; no test reads a real Arc profile.
namespace ahoi::importer::arc::test_support {

inline constexpr int kSyntheticHistoryVersion = 70;

struct SyntheticHistoryRow {
  std::string url;
  std::string title;
  int visit_count = 1;
  int typed_count = 0;
  base::Time last_visit;
  bool hidden = false;
};

// Writes the `meta` and `urls` tables of a Chromium History database into the
// open `database` (the schema subset the reader relies on).
inline bool WriteSyntheticHistory(sql::Database& database,
                                  const std::vector<SyntheticHistoryRow>& rows,
                                  int version = kSyntheticHistoryVersion) {
  if (!database.Execute("CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE "
                        "PRIMARY KEY, value LONGVARCHAR)") ||
      !database.Execute(
          base::StrCat({"INSERT INTO meta(key, value) VALUES('version', '",
                        base::NumberToString(version), "')"})) ||
      !database.Execute(
          "CREATE TABLE urls(id INTEGER PRIMARY KEY AUTOINCREMENT, "
          "url LONGVARCHAR, title LONGVARCHAR, "
          "visit_count INTEGER DEFAULT 0 NOT NULL, "
          "typed_count INTEGER DEFAULT 0 NOT NULL, "
          "last_visit_time INTEGER NOT NULL, "
          "hidden INTEGER DEFAULT 0 NOT NULL)")) {
    return false;
  }
  for (const SyntheticHistoryRow& row : rows) {
    sql::Statement insert(database.GetUniqueStatement(
        "INSERT INTO urls(url, title, visit_count, typed_count, "
        "last_visit_time, hidden) VALUES(?, ?, ?, ?, ?, ?)"));
    insert.BindString(0, row.url);
    insert.BindString(1, row.title);
    insert.BindInt(2, row.visit_count);
    insert.BindInt(3, row.typed_count);
    insert.BindInt64(
        4, row.last_visit.ToDeltaSinceWindowsEpoch().InMicroseconds());
    insert.BindBool(5, row.hidden);
    if (!insert.Run()) {
      return false;
    }
  }
  return true;
}

}  // namespace ahoi::importer::arc::test_support

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_HISTORY_UNITTEST_SUPPORT_H_
