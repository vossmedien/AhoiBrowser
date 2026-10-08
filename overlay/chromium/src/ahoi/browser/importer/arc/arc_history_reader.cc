// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_history_reader.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_source_model.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/cstring_view.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "sql/database.h"
#include "sql/statement.h"

namespace ahoi::importer::arc {

namespace {

constexpr base::cstring_view kRequiredUrlColumns[] = {
    "id",    "url", "title", "visit_count", "typed_count", "last_visit_time",
    "hidden"};

bool IsWithinSizeLimit(const base::FilePath& path, bool required) {
  if (!base::PathExists(path)) {
    return !required;
  }
  const std::optional<int64_t> size = base::GetFileSize(path);
  return size.has_value() && *size >= 0 && *size <= kMaxArcHistoryDatabaseBytes;
}

bool PassesQuickCheck(sql::Database& database) {
  sql::Statement check(database.GetUniqueStatement("PRAGMA quick_check"));
  if (!check.is_valid() || !check.Step() || check.ColumnString(0) != "ok") {
    return false;
  }
  // A healthy database reports exactly one "ok" row.
  return !check.Step() && check.Succeeded();
}

bool HasSupportedSchema(sql::Database& database) {
  if (!database.DoesTableExist("meta") || !database.DoesTableExist("urls")) {
    return false;
  }
  sql::Statement version(database.GetUniqueStatement(
      "SELECT value FROM meta WHERE key='version'"));
  if (!version.is_valid() || !version.Step()) {
    return false;
  }
  const int value = version.ColumnInt(0);
  if (value < kMinArcHistorySchemaVersion ||
      value > kMaxArcHistorySchemaVersion) {
    return false;
  }
  return std::ranges::all_of(kRequiredUrlColumns,
                             [&database](base::cstring_view column) {
                               return database.DoesColumnExist("urls", column);
                             });
}

int ClampCounter(int64_t value) {
  return static_cast<int>(
      std::clamp<int64_t>(value, 0, std::numeric_limits<int>::max()));
}

int SaturatingAdd(int left, int right) {
  return ClampCounter(static_cast<int64_t>(left) + right);
}

bool IsNewerFirst(const ArcHistoryEntry& left, const ArcHistoryEntry& right) {
  if (left.last_visit != right.last_visit) {
    return left.last_visit > right.last_visit;
  }
  return left.url.spec() < right.url.spec();
}

}  // namespace

ArcHistoryReadResult ReadArcHistoryDatabase(
    const base::FilePath& database_path,
    const ArcHistoryReadWindow& window,
    const ArcHistoryReadLimits& limits) {
  ArcHistoryReadResult result;
  if (database_path.empty() || !database_path.IsAbsolute() ||
      base::IsLink(database_path) || window.not_before.is_null() ||
      window.not_after < window.not_before) {
    result.status = ArcImportStatus::kInvalidPath;
    return result;
  }
  if (!IsWithinSizeLimit(database_path, /*required=*/true) ||
      !IsWithinSizeLimit(
          base::FilePath(database_path.value() + FILE_PATH_LITERAL("-wal")),
          /*required=*/false)) {
    result.status = ArcImportStatus::kLimitExceeded;
    return result;
  }

  // The database is a private copy; Open() may checkpoint its WAL into it.
  // Errors are expected for damaged input and end the read fail-closed.
  sql::Database database(sql::DatabaseOptions(), "AhoiArcHistoryImport");
  database.set_error_callback(
      base::BindRepeating([](int /*error*/, sql::Statement* /*statement*/) {}));
  if (!database.Open(database_path) ||
      !database.Execute("PRAGMA query_only=1") || !PassesQuickCheck(database) ||
      !HasSupportedSchema(database)) {
    result.status = ArcImportStatus::kUnsupportedSchema;
    return result;
  }

  sql::Statement count(
      database.GetUniqueStatement("SELECT COUNT(*) FROM urls"));
  if (!count.is_valid() || !count.Step()) {
    result.status = ArcImportStatus::kUnsupportedSchema;
    return result;
  }
  const int64_t source_rows = count.ColumnInt64(0);
  if (source_rows < 0 ||
      static_cast<uint64_t>(source_rows) > limits.max_scanned_rows) {
    result.status = ArcImportStatus::kLimitExceeded;
    return result;
  }
  result.stats.source_rows = static_cast<size_t>(source_rows);

  sql::Statement rows(database.GetUniqueStatement(
      "SELECT url, title, visit_count, typed_count, last_visit_time, hidden "
      "FROM urls ORDER BY last_visit_time DESC, id ASC"));
  if (!rows.is_valid()) {
    result.status = ArcImportStatus::kUnsupportedSchema;
    return result;
  }
  std::map<std::string, size_t> index_by_url;
  size_t scanned = 0;
  while (rows.Step()) {
    if (++scanned > limits.max_scanned_rows) {
      result.status = ArcImportStatus::kLimitExceeded;
      result.entries.clear();
      return result;
    }
    if (rows.ColumnInt64(5) != 0) {
      ++result.stats.hidden_rows;
      continue;
    }
    const int64_t microseconds = rows.ColumnInt64(4);
    const base::Time last_visit = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(microseconds));
    if (microseconds <= 0 || last_visit > window.not_after) {
      ++result.stats.invalid_times;
      continue;
    }
    if (last_visit < window.not_before) {
      ++result.stats.expired_rows;
      continue;
    }
    const std::string raw_url = rows.ColumnString(0);
    if (raw_url.empty() || raw_url.size() > kMaxUrlBytes ||
        !internal::IsSafeImportUrl(raw_url)) {
      ++result.stats.unsafe_urls;
      continue;
    }
    GURL url(raw_url);
    // Canonicalization may map distinct source strings to one page. Rows
    // arrive newest first, so the first occurrence already holds the latest
    // visit; later ones only add their counters.
    const auto existing = index_by_url.find(url.spec());
    if (existing != index_by_url.end()) {
      ArcHistoryEntry& entry = result.entries[existing->second];
      entry.visit_count =
          SaturatingAdd(entry.visit_count, ClampCounter(rows.ColumnInt64(2)));
      entry.typed_count =
          SaturatingAdd(entry.typed_count, ClampCounter(rows.ColumnInt64(3)));
      ++result.stats.merged_duplicates;
      continue;
    }
    if (result.entries.size() >= limits.max_rows) {
      ++result.stats.over_limit_rows;
      continue;
    }
    std::string raw_title = rows.ColumnString(1);
    std::u16string title;
    if (raw_title.size() <= kMaxTitleBytes && base::IsStringUTF8(raw_title)) {
      title = base::UTF8ToUTF16(raw_title);
    } else {
      ++result.stats.cleared_titles;
    }
    index_by_url.emplace(url.spec(), result.entries.size());
    result.entries.push_back(
        {.url = std::move(url),
         .title = std::move(title),
         .visit_count = std::max(1, ClampCounter(rows.ColumnInt64(2))),
         .typed_count = ClampCounter(rows.ColumnInt64(3)),
         .last_visit = last_visit});
  }
  if (!rows.Succeeded()) {
    result.status = ArcImportStatus::kUnsupportedSchema;
    result.entries.clear();
    return result;
  }
  std::ranges::stable_sort(result.entries, IsNewerFirst);
  result.stats.accepted_rows = result.entries.size();
  result.status = ArcImportStatus::kOk;
  return result;
}

ArcHistoryReadResult MergeArcHistoryResults(
    std::vector<ArcHistoryReadResult> results,
    const ArcHistoryReadLimits& limits) {
  ArcHistoryReadResult merged;
  std::map<std::string, size_t> index_by_url;
  for (ArcHistoryReadResult& result : results) {
    if (result.status != ArcImportStatus::kOk) {
      return {.status = result.status};
    }
    ArcHistoryReadStats& stats = merged.stats;
    stats.source_rows += result.stats.source_rows;
    stats.hidden_rows += result.stats.hidden_rows;
    stats.unsafe_urls += result.stats.unsafe_urls;
    stats.expired_rows += result.stats.expired_rows;
    stats.invalid_times += result.stats.invalid_times;
    stats.cleared_titles += result.stats.cleared_titles;
    stats.merged_duplicates += result.stats.merged_duplicates;
    stats.over_limit_rows += result.stats.over_limit_rows;
    for (ArcHistoryEntry& entry : result.entries) {
      const auto [existing, inserted] =
          index_by_url.emplace(entry.url.spec(), merged.entries.size());
      if (inserted) {
        merged.entries.push_back(std::move(entry));
        continue;
      }
      ArcHistoryEntry& kept = merged.entries[existing->second];
      ++stats.merged_duplicates;
      const int visit_count =
          SaturatingAdd(kept.visit_count, entry.visit_count);
      const int typed_count =
          SaturatingAdd(kept.typed_count, entry.typed_count);
      if (entry.last_visit > kept.last_visit) {
        kept = std::move(entry);
      }
      kept.visit_count = visit_count;
      kept.typed_count = typed_count;
    }
  }
  std::ranges::stable_sort(merged.entries, IsNewerFirst);
  if (merged.entries.size() > limits.max_rows) {
    merged.stats.over_limit_rows += merged.entries.size() - limits.max_rows;
    merged.entries.resize(limits.max_rows);
  }
  merged.stats.accepted_rows = merged.entries.size();
  merged.status = ArcImportStatus::kOk;
  return merged;
}

}  // namespace ahoi::importer::arc
