// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_history_journal.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/importer/arc/arc_import_safe_files.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/values.h"

namespace ahoi::importer::arc {

namespace {

constexpr int kJournalVersion = 1;
constexpr int64_t kMaxJournalBytes = 64 * 1024;
constexpr char kJournalDirectory[] = "Ahoi";
constexpr char kJournalFilename[] = "ArcHistoryImportJournal.json";

bool IsLowerSha256(std::string_view value) {
  return value.size() == 64 && std::ranges::all_of(value, [](char character) {
           return base::IsAsciiDigit(character) ||
                  (character >= 'a' && character <= 'f');
         });
}

bool IsSafeBackupIdentifier(std::string_view value) {
  return value.size() >= 16 && value.size() <= 64 &&
         std::ranges::all_of(value, [](char character) {
           return base::IsAsciiDigit(character) ||
                  (character >= 'a' && character <= 'f') || character == '-';
         });
}

base::FilePath JournalPath(const base::FilePath& profile_path) {
  return profile_path.AppendASCII(kJournalDirectory)
      .AppendASCII(kJournalFilename);
}

base::DictValue MetricsToValue(const ArcHistoryImportMetrics& metrics) {
  base::DictValue value;
  value.Set("added_pages", metrics.added_pages);
  value.Set("created_urls", metrics.created_urls);
  value.Set("deduplicated_pages", metrics.deduplicated_pages);
  value.Set("expired_pages", metrics.expired_pages);
  value.Set("excluded_pages", metrics.excluded_pages);
  return value;
}

std::optional<ArcHistoryImportMetrics> MetricsFromValue(
    const base::DictValue* value) {
  if (!value || value->size() != 5u) {
    return std::nullopt;
  }
  ArcHistoryImportMetrics metrics;
  for (auto [key, field] :
       {std::pair<std::string_view, int*>{"added_pages", &metrics.added_pages},
        {"created_urls", &metrics.created_urls},
        {"deduplicated_pages", &metrics.deduplicated_pages},
        {"expired_pages", &metrics.expired_pages},
        {"excluded_pages", &metrics.excluded_pages}}) {
    const std::optional<int> number = value->FindInt(key);
    if (!number || *number < 0) {
      return std::nullopt;
    }
    *field = *number;
  }
  return metrics;
}

base::DictValue CommittedToValue(const ArcHistoryCommittedState& committed) {
  base::DictValue value;
  value.Set("history_key", committed.history_key);
  value.Set("metrics", MetricsToValue(committed.metrics));
  return value;
}

std::optional<ArcHistoryCommittedState> CommittedFromValue(
    const base::DictValue* value,
    size_t expected_size) {
  const std::string* key = value ? value->FindString("history_key") : nullptr;
  if (!value || value->size() != expected_size || !key ||
      !IsLowerSha256(*key)) {
    return std::nullopt;
  }
  std::optional<ArcHistoryImportMetrics> metrics =
      MetricsFromValue(value->FindDict("metrics"));
  if (!metrics) {
    return std::nullopt;
  }
  return ArcHistoryCommittedState{.history_key = *key, .metrics = *metrics};
}

bool IsValidPrepared(const ArcHistoryPreparedState& prepared) {
  return IsLowerSha256(prepared.history_key) &&
         IsSafeBackupIdentifier(prepared.backup_identifier) &&
         IsLowerSha256(prepared.manifest_sha256) &&
         IsLowerSha256(prepared.snapshot_sha256) &&
         (!prepared.previous_committed ||
          IsLowerSha256(prepared.previous_committed->history_key));
}

bool EnsureJournalDirectory(const base::FilePath& profile_path) {
  const base::FilePath directory = profile_path.AppendASCII(kJournalDirectory);
  if (base::IsLink(directory)) {
    return false;
  }
  if (!base::DirectoryExists(directory) &&
      (!base::CreateDirectory(directory) ||
       !base::SetPosixFilePermissions(directory, 0700))) {
    return false;
  }
  return safe_files::IsOwnerOnlyDirectory(directory);
}

bool WriteJournal(const base::FilePath& profile_path, base::DictValue value) {
  value.Set("version", kJournalVersion);
  std::string json;
  if (profile_path.empty() || !profile_path.IsAbsolute() ||
      !EnsureJournalDirectory(profile_path) ||
      !base::JSONWriter::Write(value, &json) ||
      json.size() > static_cast<size_t>(kMaxJournalBytes)) {
    return false;
  }
  const base::FilePath path = JournalPath(profile_path);
  if (base::IsLink(path)) {
    return false;
  }
  // The temporary file is created 0600 and renamed over the journal.
  return base::ImportantFileWriter::WriteFileAtomically(path, json) &&
         base::SetPosixFilePermissions(path, 0600) &&
         safe_files::FsyncDirectory(path.DirName());
}

}  // namespace

ArcHistoryJournalReadResult ReadArcHistoryJournal(
    const base::FilePath& profile_path) {
  const ArcHistoryJournalReadResult error{.status =
                                              ArcImportStatus::kJournalError};
  const base::FilePath path = JournalPath(profile_path);
  if (profile_path.empty() || !profile_path.IsAbsolute()) {
    return error;
  }
  if (safe_files::IsPathMissingNoFollow(path.DirName()) ||
      safe_files::IsPathMissingNoFollow(path)) {
    return {};
  }
  base::File file = safe_files::OpenSafeRegularFile(path);
  std::string json;
  if (!safe_files::IsOwnerOnlyDirectory(path.DirName()) ||
      !safe_files::ReadStableOwnerOnlyFile(&file, kMaxJournalBytes, &json)) {
    return error;
  }
  const std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  const base::DictValue* dict = parsed ? parsed->GetIfDict() : nullptr;
  const std::optional<int> version =
      dict ? dict->FindInt("version") : std::nullopt;
  const std::string* state = dict ? dict->FindString("state") : nullptr;
  if (!version || *version != kJournalVersion || !state) {
    return error;
  }
  ArcHistoryJournalReadResult result;
  if (*state == "committed") {
    // version, state, history_key, metrics.
    result.committed = CommittedFromValue(dict, 4u);
    return result.committed ? result : error;
  }
  if (*state != "prepared") {
    return error;
  }
  const base::DictValue* previous = dict->FindDict("previous");
  const std::string* key = dict->FindString("history_key");
  const std::string* backup = dict->FindString("backup_identifier");
  const std::string* manifest = dict->FindString("manifest_sha256");
  const std::string* snapshot = dict->FindString("snapshot_sha256");
  if (dict->size() != (previous ? 7u : 6u) || !key || !backup || !manifest ||
      !snapshot) {
    return error;
  }
  ArcHistoryPreparedState prepared{.history_key = *key,
                                   .backup_identifier = *backup,
                                   .manifest_sha256 = *manifest,
                                   .snapshot_sha256 = *snapshot};
  if (previous) {
    prepared.previous_committed = CommittedFromValue(previous, 2u);
    if (!prepared.previous_committed) {
      return error;
    }
  }
  if (!IsValidPrepared(prepared)) {
    return error;
  }
  result.prepared = std::move(prepared);
  return result;
}

bool WriteArcHistoryPreparedJournal(const base::FilePath& profile_path,
                                    const ArcHistoryPreparedState& prepared) {
  if (!IsValidPrepared(prepared)) {
    return false;
  }
  base::DictValue value;
  value.Set("state", "prepared");
  value.Set("history_key", prepared.history_key);
  value.Set("backup_identifier", prepared.backup_identifier);
  value.Set("manifest_sha256", prepared.manifest_sha256);
  value.Set("snapshot_sha256", prepared.snapshot_sha256);
  if (prepared.previous_committed) {
    value.Set("previous", CommittedToValue(*prepared.previous_committed));
  }
  return WriteJournal(profile_path, std::move(value));
}

bool WriteArcHistoryCommittedJournal(
    const base::FilePath& profile_path,
    const ArcHistoryCommittedState& committed) {
  if (!IsLowerSha256(committed.history_key)) {
    return false;
  }
  base::DictValue value = CommittedToValue(committed);
  value.Set("state", "committed");
  return WriteJournal(profile_path, std::move(value));
}

bool RestoreArcHistoryJournal(
    const base::FilePath& profile_path,
    const std::optional<ArcHistoryCommittedState>& previous) {
  if (previous) {
    return WriteArcHistoryCommittedJournal(profile_path, *previous);
  }
  const base::FilePath path = JournalPath(profile_path);
  if (safe_files::IsPathMissingNoFollow(path)) {
    return true;
  }
  return !base::IsLink(path) && base::DeleteFile(path) &&
         safe_files::FsyncDirectory(path.DirName());
}

}  // namespace ahoi::importer::arc
