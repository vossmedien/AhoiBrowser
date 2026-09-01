// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_journal.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "ahoi/browser/importer/arc/arc_import_tree_fingerprint.h"
#include "base/containers/span.h"
#include "base/files/scoped_file.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/notreached.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "base/values.h"

namespace ahoi::importer::arc {

namespace {

constexpr int kJournalSchemaVersion = 5;
constexpr int64_t kMaxJournalBytes = 1024 * 1024;
constexpr size_t kMaxAffectedIds = kMaxItemCount + kMaxWorkspaceCount;
constexpr char kJournalDirectory[] = "Ahoi";
constexpr char kJournalFilename[] = "ArcImportJournal.json";
constexpr char kJournalTemporaryPrefix[] = ".ArcImportJournal.json.tmp-";

bool IsLowerSha256(const std::string& value) {
  return value.size() == 64 && std::ranges::all_of(value, [](char character) {
           return base::IsHexDigit(character) &&
                  (base::IsAsciiDigit(character) ||
                   base::IsAsciiLower(character));
         });
}

bool IsCanonicalUuid(const std::string& value) {
  const base::Uuid uuid = base::Uuid::ParseLowercase(value);
  return uuid.is_valid() && uuid.AsLowercaseString() == value;
}

bool IsSafeBackupIdentifier(const std::string& value) {
  return value.size() >= 16 && value.size() <= 64 &&
         std::ranges::all_of(value, [](char character) {
           return base::IsAsciiDigit(character) ||
                  (character >= 'a' && character <= 'f') || character == '-';
         });
}

const char* PreparedPhaseToString(ArcImportPreparedPhase phase) {
  switch (phase) {
    case ArcImportPreparedPhase::kTreeOnly:
      return "tree_only";
    case ArcImportPreparedPhase::kRuntimeMayHaveStarted:
      return "runtime_may_have_started";
    case ArcImportPreparedPhase::kRuntimePersisted:
      return "runtime_persisted";
    case ArcImportPreparedPhase::kManualRecoveryRequired:
      return "manual_recovery_required";
  }
  NOTREACHED();
}

bool IsValidPreparedPhase(ArcImportPreparedPhase phase,
                          bool runtime_mutation_planned) {
  switch (phase) {
    case ArcImportPreparedPhase::kTreeOnly:
    case ArcImportPreparedPhase::kManualRecoveryRequired:
      return true;
    case ArcImportPreparedPhase::kRuntimeMayHaveStarted:
    case ArcImportPreparedPhase::kRuntimePersisted:
      return runtime_mutation_planned;
  }
  return false;
}

std::optional<ArcImportPreparedPhase> PreparedPhaseFromString(
    std::string_view phase) {
  if (phase == "tree_only") {
    return ArcImportPreparedPhase::kTreeOnly;
  }
  if (phase == "runtime_may_have_started") {
    return ArcImportPreparedPhase::kRuntimeMayHaveStarted;
  }
  if (phase == "runtime_persisted") {
    return ArcImportPreparedPhase::kRuntimePersisted;
  }
  if (phase == "manual_recovery_required") {
    return ArcImportPreparedPhase::kManualRecoveryRequired;
  }
  return std::nullopt;
}

bool SameOpenedFileGeneration(const struct stat& before,
                              const struct stat& after) {
  return before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
         before.st_size == after.st_size &&
         before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec &&
         before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec &&
         before.st_ctimespec.tv_sec == after.st_ctimespec.tv_sec &&
         before.st_ctimespec.tv_nsec == after.st_ctimespec.tv_nsec;
}

base::ScopedFD OpenJournalDirectory(const base::FilePath& profile_path,
                                    bool create,
                                    bool* missing) {
  *missing = false;
  const base::FilePath directory = profile_path.AppendASCII(kJournalDirectory);
  bool created = false;
  if (create) {
    if (mkdir(directory.value().c_str(), 0700) == 0) {
      created = true;
    } else if (errno != EEXIST) {
      return {};
    }
  }
  base::ScopedFD directory_fd(HANDLE_EINTR(
      open(directory.value().c_str(),
           O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK)));
  if (!directory_fd.is_valid()) {
    *missing = !create && errno == ENOENT;
    return {};
  }
  struct stat directory_stat;
  if (fstat(directory_fd.get(), &directory_stat) != 0 ||
      !S_ISDIR(directory_stat.st_mode) || directory_stat.st_uid != getuid() ||
      (!created && (directory_stat.st_mode & 0077) != 0) ||
      (created && fchmod(directory_fd.get(), 0700) != 0)) {
    return {};
  }
  if (created) {
    base::ScopedFD profile_fd(HANDLE_EINTR(
        open(profile_path.value().c_str(),
             O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK)));
    struct stat profile_stat;
    if (!profile_fd.is_valid() || fstat(profile_fd.get(), &profile_stat) != 0 ||
        !S_ISDIR(profile_stat.st_mode) || profile_stat.st_uid != getuid() ||
        HANDLE_EINTR(fsync(profile_fd.get())) != 0) {
      return {};
    }
  }
  return directory_fd;
}

bool ReadOpenedJournal(int descriptor,
                       const struct stat& before,
                       std::string* json) {
  if (!S_ISREG(before.st_mode) || before.st_uid != getuid() ||
      before.st_nlink != 1 || (before.st_mode & 0077) != 0 ||
      before.st_size <= 0 || before.st_size > kMaxJournalBytes) {
    return false;
  }
  json->resize(static_cast<size_t>(before.st_size));
  size_t offset = 0;
  while (offset < json->size()) {
    const base::span<char> remaining = base::span(*json).subspan(offset);
    const ssize_t read_size =
        HANDLE_EINTR(read(descriptor, remaining.data(), remaining.size()));
    if (read_size <= 0) {
      return false;
    }
    offset += static_cast<size_t>(read_size);
  }
  char extra = 0;
  if (HANDLE_EINTR(read(descriptor, &extra, 1)) != 0) {
    return false;
  }
  struct stat after;
  return fstat(descriptor, &after) == 0 && after.st_nlink == 1 &&
         SameOpenedFileGeneration(before, after);
}

std::optional<int> ReadNonNegativeInt(const base::DictValue& dict,
                                      std::string_view key) {
  const std::optional<int> value = dict.FindInt(key);
  return value.has_value() && *value >= 0 ? value : std::nullopt;
}

base::DictValue MetricsToValue(const ArcImportJournalMetrics& metrics) {
  base::DictValue value;
  value.Set("workspaces", metrics.workspaces);
  value.Set("folders", metrics.folders);
  value.Set("pages", metrics.pages);
  value.Set("splits", metrics.splits);
  value.Set("degraded_splits", metrics.degraded_splits);
  value.Set("renamed_workspaces", metrics.renamed_workspaces);
  value.Set("skipped_workspaces", metrics.skipped_workspaces);
  value.Set("merged_workspaces", metrics.merged_workspaces);
  value.Set("reconstructed_splits", metrics.reconstructed_splits);
  value.Set("approximated_four_pane_ratios",
            metrics.approximated_four_pane_ratios);
  return value;
}

std::optional<ArcImportJournalMetrics> MetricsFromValue(
    const base::DictValue* value) {
  if (!value) {
    return std::nullopt;
  }
  const std::optional<int> workspaces =
      ReadNonNegativeInt(*value, "workspaces");
  const std::optional<int> folders = ReadNonNegativeInt(*value, "folders");
  const std::optional<int> pages = ReadNonNegativeInt(*value, "pages");
  const std::optional<int> splits = ReadNonNegativeInt(*value, "splits");
  const std::optional<int> degraded =
      ReadNonNegativeInt(*value, "degraded_splits");
  const std::optional<int> renamed =
      ReadNonNegativeInt(*value, "renamed_workspaces");
  const std::optional<int> skipped =
      ReadNonNegativeInt(*value, "skipped_workspaces");
  const std::optional<int> merged =
      ReadNonNegativeInt(*value, "merged_workspaces");
  const std::optional<int> reconstructed =
      ReadNonNegativeInt(*value, "reconstructed_splits");
  const std::optional<int> approximated =
      ReadNonNegativeInt(*value, "approximated_four_pane_ratios");
  if (!workspaces || !folders || !pages || !splits || !degraded || !renamed ||
      !skipped || !merged || !reconstructed || !approximated) {
    return std::nullopt;
  }
  return ArcImportJournalMetrics{
      .workspaces = *workspaces,
      .folders = *folders,
      .pages = *pages,
      .splits = *splits,
      .degraded_splits = *degraded,
      .renamed_workspaces = *renamed,
      .skipped_workspaces = *skipped,
      .merged_workspaces = *merged,
      .reconstructed_splits = *reconstructed,
      .approximated_four_pane_ratios = *approximated};
}

ArcImportJournalMetrics MetricsForResult(const ArcImportCommitResult& result) {
  const auto checked = [](size_t value) {
    return value <= static_cast<size_t>(std::numeric_limits<int>::max())
               ? static_cast<int>(value)
               : -1;
  };
  return {.workspaces = checked(result.stats.imported_workspace_count),
          .folders = checked(result.stats.imported_folder_count),
          .pages = checked(result.stats.imported_page_count),
          .splits = checked(result.stats.imported_split_count),
          .degraded_splits = checked(result.stats.degraded_split_count),
          .renamed_workspaces = checked(result.renamed_workspace_count),
          .skipped_workspaces = checked(result.skipped_workspace_count),
          .merged_workspaces = checked(result.merged_workspace_count),
          .reconstructed_splits = checked(result.reconstructed_split_count),
          .approximated_four_pane_ratios =
              checked(result.approximated_four_pane_ratio_count)};
}

bool MetricsAreValid(const ArcImportJournalMetrics& metrics) {
  return metrics.workspaces >= 0 && metrics.folders >= 0 &&
         metrics.pages >= 0 && metrics.splits >= 0 &&
         metrics.degraded_splits >= 0 && metrics.renamed_workspaces >= 0 &&
         metrics.skipped_workspaces >= 0 && metrics.merged_workspaces >= 0 &&
         metrics.reconstructed_splits >= 0 &&
         metrics.approximated_four_pane_ratios >= 0;
}

base::DictValue CommittedToValue(const ArcImportCommittedState& committed) {
  base::DictValue value;
  value.Set("snapshot_sha256", committed.snapshot_hash);
  value.Set("selection_fingerprint", committed.selection_fingerprint);
  value.Set("idempotency_key", committed.idempotency_key);
  value.Set("metrics", MetricsToValue(committed.metrics));
  return value;
}

std::optional<ArcImportCommittedState> CommittedFromValue(
    const base::DictValue* value) {
  const std::string* snapshot =
      value ? value->FindString("snapshot_sha256") : nullptr;
  const std::string* selection =
      value ? value->FindString("selection_fingerprint") : nullptr;
  const std::string* idempotency =
      value ? value->FindString("idempotency_key") : nullptr;
  std::optional<ArcImportJournalMetrics> metrics =
      value ? MetricsFromValue(value->FindDict("metrics")) : std::nullopt;
  if (!snapshot || !IsLowerSha256(*snapshot) || !selection ||
      !IsLowerSha256(*selection) || !idempotency ||
      !IsLowerSha256(*idempotency) || !metrics) {
    return std::nullopt;
  }
  return ArcImportCommittedState{.snapshot_hash = *snapshot,
                                 .selection_fingerprint = *selection,
                                 .idempotency_key = *idempotency,
                                 .metrics = *metrics};
}

std::optional<ArcImportCommittedState> LegacyCommittedFromValue(
    const base::DictValue& value) {
  const std::string* snapshot = value.FindString("snapshot_sha256");
  if (!snapshot || !IsLowerSha256(*snapshot)) {
    return std::nullopt;
  }
  ArcImportJournalMetrics metrics;
  const auto read_or_zero = [&value](std::string_view key) {
    return ReadNonNegativeInt(value, key).value_or(0);
  };
  metrics.workspaces = read_or_zero("workspaces");
  metrics.folders = read_or_zero("folders");
  metrics.pages = read_or_zero("pages");
  metrics.splits = read_or_zero("splits");
  metrics.degraded_splits = read_or_zero("degraded_splits");
  metrics.renamed_workspaces = read_or_zero("renamed_workspaces");
  metrics.skipped_workspaces = read_or_zero("skipped_workspaces");
  metrics.merged_workspaces = read_or_zero("merged_workspaces");
  metrics.reconstructed_splits = read_or_zero("reconstructed_splits");
  return ArcImportCommittedState{.snapshot_hash = *snapshot,
                                 .selection_fingerprint = *snapshot,
                                 .idempotency_key = *snapshot,
                                 .metrics = metrics};
}

bool IsValidCommitted(const ArcImportCommittedState& committed) {
  return IsLowerSha256(committed.snapshot_hash) &&
         IsLowerSha256(committed.selection_fingerprint) &&
         IsLowerSha256(committed.idempotency_key) &&
         MetricsAreValid(committed.metrics);
}

bool IsValidPrepared(const ArcImportPreparedState& prepared) {
  if (!IsCanonicalUuid(prepared.transaction_id) ||
      !IsLowerSha256(prepared.snapshot_hash) ||
      !IsLowerSha256(prepared.selection_fingerprint) ||
      !IsLowerSha256(prepared.idempotency_key) ||
      !IsSafeBackupIdentifier(prepared.backup_identifier) ||
      !base::StartsWith(prepared.backup_identifier,
                        prepared.snapshot_hash.substr(0, 12) + "-") ||
      !IsLowerSha256(prepared.manifest_sha256) ||
      !IsArcImportTreeFingerprint(prepared.previous_tree_sha256) ||
      !IsArcImportTreeFingerprint(prepared.expected_tree_sha256) ||
      prepared.affected_ids.size() > kMaxAffectedIds ||
      !IsValidPreparedPhase(prepared.phase,
                            prepared.runtime_mutation_planned) ||
      (prepared.previous_committed &&
       !IsValidCommitted(*prepared.previous_committed)) ||
      (prepared.intended_committed &&
       !IsValidCommitted(*prepared.intended_committed))) {
    return false;
  }
  const bool receipt_is_required =
      prepared.phase == ArcImportPreparedPhase::kRuntimePersisted;
  const bool receipt_is_present = IsLowerSha256(prepared.native_receipt_sha256);
  if (prepared.runtime_mutation_planned) {
    if (!IsLowerSha256(prepared.expected_native_structure_sha256) ||
        prepared.native_member_ids.empty() ||
        prepared.native_member_ids.size() > kMaxAffectedIds ||
        (receipt_is_required && !receipt_is_present) ||
        (prepared.phase != ArcImportPreparedPhase::kRuntimePersisted &&
         prepared.phase != ArcImportPreparedPhase::kManualRecoveryRequired &&
         !prepared.native_receipt_sha256.empty()) ||
        (prepared.phase == ArcImportPreparedPhase::kManualRecoveryRequired &&
         !prepared.native_receipt_sha256.empty() && !receipt_is_present)) {
      return false;
    }
  } else if (!prepared.expected_native_structure_sha256.empty() ||
             !prepared.native_receipt_sha256.empty() ||
             !prepared.native_member_ids.empty() || receipt_is_required ||
             prepared.phase == ArcImportPreparedPhase::kRuntimeMayHaveStarted) {
    return false;
  }
  if (receipt_is_required) {
    if (!prepared.intended_committed ||
        prepared.intended_committed->snapshot_hash != prepared.snapshot_hash ||
        prepared.intended_committed->selection_fingerprint !=
            prepared.selection_fingerprint ||
        prepared.intended_committed->idempotency_key !=
            prepared.idempotency_key) {
      return false;
    }
  } else if (prepared.phase !=
                 ArcImportPreparedPhase::kManualRecoveryRequired &&
             prepared.intended_committed) {
    return false;
  }
  std::set<std::string> ids;
  for (const std::string& id : prepared.affected_ids) {
    if (!IsCanonicalUuid(id) || !ids.insert(id).second) {
      return false;
    }
  }
  if (!std::ranges::is_sorted(prepared.affected_ids)) {
    return false;
  }
  std::set<std::string> native_ids;
  for (const std::string& id : prepared.native_member_ids) {
    if (!IsCanonicalUuid(id) || !ids.contains(id) ||
        !native_ids.insert(id).second) {
      return false;
    }
  }
  return std::ranges::is_sorted(prepared.native_member_ids);
}

base::DictValue BuildCommittedJournal(
    const ArcImportCommittedState& committed) {
  base::DictValue journal;
  journal.Set("version", kJournalSchemaVersion);
  journal.Set("status", "committed");
  journal.Merge(CommittedToValue(committed));
  return journal;
}

base::DictValue BuildPreparedJournal(const ArcImportPreparedState& prepared) {
  base::DictValue journal;
  journal.Set("version", kJournalSchemaVersion);
  journal.Set("status", "prepared");
  journal.Set("transaction_id", prepared.transaction_id);
  journal.Set("snapshot_sha256", prepared.snapshot_hash);
  journal.Set("selection_fingerprint", prepared.selection_fingerprint);
  journal.Set("idempotency_key", prepared.idempotency_key);
  journal.Set("backup_id", prepared.backup_identifier);
  journal.Set("manifest_sha256", prepared.manifest_sha256);
  journal.Set("previous_tree_sha256", prepared.previous_tree_sha256);
  journal.Set("expected_tree_sha256", prepared.expected_tree_sha256);
  journal.Set("expected_native_structure_sha256",
              prepared.expected_native_structure_sha256);
  journal.Set("native_receipt_sha256", prepared.native_receipt_sha256);
  journal.Set("phase", PreparedPhaseToString(prepared.phase));
  journal.Set("runtime_mutation_planned", prepared.runtime_mutation_planned);
  base::ListValue affected_ids;
  for (const std::string& id : prepared.affected_ids) {
    affected_ids.Append(id);
  }
  journal.Set("affected_ids", std::move(affected_ids));
  base::ListValue native_member_ids;
  for (const std::string& id : prepared.native_member_ids) {
    native_member_ids.Append(id);
  }
  journal.Set("native_member_ids", std::move(native_member_ids));
  if (prepared.previous_committed) {
    journal.Set("previous_committed",
                CommittedToValue(*prepared.previous_committed));
  }
  if (prepared.intended_committed) {
    journal.Set("intended_committed",
                CommittedToValue(*prepared.intended_committed));
  }
  return journal;
}

bool AtomicWriteJournal(const base::FilePath& profile_path,
                        base::DictValue journal) {
  std::string json;
  if (!base::JSONWriter::Write(journal, &json) || json.empty() ||
      json.size() > static_cast<size_t>(kMaxJournalBytes)) {
    return false;
  }
  bool directory_missing = false;
  base::ScopedFD directory_fd =
      OpenJournalDirectory(profile_path, true, &directory_missing);
  if (!directory_fd.is_valid()) {
    return false;
  }
  struct stat existing_stat;
  if (fstatat(directory_fd.get(), kJournalFilename, &existing_stat,
              AT_SYMLINK_NOFOLLOW) == 0) {
    if (!S_ISREG(existing_stat.st_mode) || existing_stat.st_uid != getuid() ||
        existing_stat.st_nlink != 1 || (existing_stat.st_mode & 0077) != 0) {
      return false;
    }
  } else if (errno != ENOENT) {
    return false;
  }

  const std::string temporary_name =
      kJournalTemporaryPrefix +
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  base::ScopedFD temporary_fd(HANDLE_EINTR(
      openat(directory_fd.get(), temporary_name.c_str(),
             O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK,
             0600)));
  if (!temporary_fd.is_valid()) {
    return false;
  }
  bool renamed = false;
  bool success = fchmod(temporary_fd.get(), 0600) == 0;
  size_t offset = 0;
  while (success && offset < json.size()) {
    const base::span<const char> remaining = base::span(json).subspan(offset);
    const ssize_t written = HANDLE_EINTR(
        write(temporary_fd.get(), remaining.data(), remaining.size()));
    success = written > 0;
    if (success) {
      offset += static_cast<size_t>(written);
    }
  }
  success = success && HANDLE_EINTR(fsync(temporary_fd.get())) == 0;
  struct stat temporary_stat;
  success = success && fstat(temporary_fd.get(), &temporary_stat) == 0 &&
            S_ISREG(temporary_stat.st_mode) &&
            temporary_stat.st_uid == getuid() && temporary_stat.st_nlink == 1 &&
            temporary_stat.st_size == static_cast<off_t>(json.size()) &&
            (temporary_stat.st_mode & 0077) == 0;
  if (success) {
    renamed = renameat(directory_fd.get(), temporary_name.c_str(),
                       directory_fd.get(), kJournalFilename) == 0;
    success = renamed && HANDLE_EINTR(fsync(directory_fd.get())) == 0;
  }
  temporary_fd.reset();
  if (!renamed) {
    unlinkat(directory_fd.get(), temporary_name.c_str(), 0);
  }
  return success;
}

bool DeleteJournal(const base::FilePath& profile_path) {
  bool directory_missing = false;
  base::ScopedFD directory_fd =
      OpenJournalDirectory(profile_path, false, &directory_missing);
  if (!directory_fd.is_valid()) {
    return directory_missing;
  }
  struct stat state;
  if (fstatat(directory_fd.get(), kJournalFilename, &state,
              AT_SYMLINK_NOFOLLOW) != 0) {
    return errno == ENOENT;
  }
  if (!S_ISREG(state.st_mode) || state.st_uid != getuid() ||
      state.st_nlink != 1 || (state.st_mode & 0077) != 0 ||
      unlinkat(directory_fd.get(), kJournalFilename, 0) != 0) {
    return false;
  }
  return HANDLE_EINTR(fsync(directory_fd.get())) == 0;
}

}  // namespace

ArcImportJournalReadResult ReadArcImportJournal(
    const base::FilePath& profile_path) {
  bool directory_missing = false;
  base::ScopedFD directory_fd =
      OpenJournalDirectory(profile_path, false, &directory_missing);
  if (!directory_fd.is_valid()) {
    return directory_missing ? ArcImportJournalReadResult()
                             : ArcImportJournalReadResult{
                                   .status = ArcImportStatus::kJournalError};
  }
  base::ScopedFD journal_fd(
      HANDLE_EINTR(openat(directory_fd.get(), kJournalFilename,
                          O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  if (!journal_fd.is_valid() && errno == ENOENT) {
    return {};
  }
  struct stat before;
  if (!journal_fd.is_valid() || fstat(journal_fd.get(), &before) != 0) {
    return {.status = ArcImportStatus::kJournalError};
  }
  std::string json;
  if (!ReadOpenedJournal(journal_fd.get(), before, &json)) {
    return {.status = ArcImportStatus::kJournalError};
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  const base::DictValue* dict =
      parsed.has_value() ? parsed->GetIfDict() : nullptr;
  const std::optional<int> version =
      dict ? dict->FindInt("version") : std::nullopt;
  const std::string* status = dict ? dict->FindString("status") : nullptr;
  if (!dict || !version.has_value() || !status) {
    return {.status = ArcImportStatus::kJournalError};
  }
  if (*version == 1 && *status == "committed") {
    std::optional<ArcImportCommittedState> committed =
        LegacyCommittedFromValue(*dict);
    return committed
               ? ArcImportJournalReadResult{.state = ArcImportJournalState::
                                                kCommitted,
                                            .committed = std::move(committed)}
               : ArcImportJournalReadResult{.status =
                                                ArcImportStatus::kJournalError};
  }
  if (*version == 2 && *status == "committed") {
    std::optional<ArcImportCommittedState> committed = CommittedFromValue(dict);
    return committed
               ? ArcImportJournalReadResult{.state = ArcImportJournalState::
                                                kCommitted,
                                            .committed = std::move(committed)}
               : ArcImportJournalReadResult{.status =
                                                ArcImportStatus::kJournalError};
  }
  if (*version == 3 && *status == "committed") {
    std::optional<ArcImportCommittedState> committed = CommittedFromValue(dict);
    return committed
               ? ArcImportJournalReadResult{.state = ArcImportJournalState::
                                                kCommitted,
                                            .committed = std::move(committed)}
               : ArcImportJournalReadResult{.status =
                                                ArcImportStatus::kJournalError};
  }
  if (*version == 4 && *status == "committed") {
    std::optional<ArcImportCommittedState> committed = CommittedFromValue(dict);
    return committed
               ? ArcImportJournalReadResult{.state = ArcImportJournalState::
                                                kCommitted,
                                            .committed = std::move(committed)}
               : ArcImportJournalReadResult{.status =
                                                ArcImportStatus::kJournalError};
  }
  if ((*version == 2 || *version == 3 || *version == 4) &&
      *status == "prepared") {
    // Version 2 did not bind either tree, and version 3 did not bind the
    // native current-session receipt. Version 4 did not persist the exact new
    // committed-state intent. None may authorize mutation or publication
    // after an upgrade; retain the durable gate as an explicit manual
    // recovery state.
    return {.state = ArcImportJournalState::kPrepared,
            .prepared = ArcImportPreparedState{
                .phase = ArcImportPreparedPhase::kManualRecoveryRequired}};
  }
  if (*version != kJournalSchemaVersion) {
    return {.status = ArcImportStatus::kJournalError};
  }
  if (*status == "committed") {
    std::optional<ArcImportCommittedState> committed = CommittedFromValue(dict);
    return committed
               ? ArcImportJournalReadResult{.state = ArcImportJournalState::
                                                kCommitted,
                                            .committed = std::move(committed)}
               : ArcImportJournalReadResult{.status =
                                                ArcImportStatus::kJournalError};
  }
  if (*status != "prepared") {
    return {.status = ArcImportStatus::kJournalError};
  }
  const std::string* transaction_id = dict->FindString("transaction_id");
  const std::string* snapshot_hash = dict->FindString("snapshot_sha256");
  const std::string* selection = dict->FindString("selection_fingerprint");
  const std::string* idempotency = dict->FindString("idempotency_key");
  const std::string* backup_id = dict->FindString("backup_id");
  const std::string* manifest_hash = dict->FindString("manifest_sha256");
  const std::string* previous_tree_hash =
      dict->FindString("previous_tree_sha256");
  const std::string* expected_tree_hash =
      dict->FindString("expected_tree_sha256");
  const std::string* expected_native_structure_hash =
      dict->FindString("expected_native_structure_sha256");
  const std::string* native_receipt_hash =
      dict->FindString("native_receipt_sha256");
  const std::string* phase = dict->FindString("phase");
  const std::optional<bool> runtime_planned =
      dict->FindBool("runtime_mutation_planned");
  const base::ListValue* ids = dict->FindList("affected_ids");
  const base::ListValue* native_ids = dict->FindList("native_member_ids");
  if (!transaction_id || !snapshot_hash || !selection || !idempotency ||
      !backup_id || !manifest_hash || !previous_tree_hash ||
      !expected_tree_hash || !expected_native_structure_hash ||
      !native_receipt_hash || !phase || !runtime_planned || !ids ||
      !native_ids || ids->size() > kMaxAffectedIds ||
      native_ids->size() > kMaxAffectedIds) {
    return {.status = ArcImportStatus::kJournalError};
  }
  const std::optional<ArcImportPreparedPhase> parsed_phase =
      PreparedPhaseFromString(*phase);
  if (!parsed_phase) {
    return {.status = ArcImportStatus::kJournalError};
  }
  ArcImportPreparedState prepared{
      .transaction_id = *transaction_id,
      .snapshot_hash = *snapshot_hash,
      .selection_fingerprint = *selection,
      .idempotency_key = *idempotency,
      .backup_identifier = *backup_id,
      .manifest_sha256 = *manifest_hash,
      .previous_tree_sha256 = *previous_tree_hash,
      .expected_tree_sha256 = *expected_tree_hash,
      .expected_native_structure_sha256 = *expected_native_structure_hash,
      .native_receipt_sha256 = *native_receipt_hash,
      .phase = *parsed_phase,
      .runtime_mutation_planned = *runtime_planned};
  for (const base::Value& id : *ids) {
    if (!id.is_string()) {
      return {.status = ArcImportStatus::kJournalError};
    }
    prepared.affected_ids.push_back(id.GetString());
  }
  for (const base::Value& id : *native_ids) {
    if (!id.is_string()) {
      return {.status = ArcImportStatus::kJournalError};
    }
    prepared.native_member_ids.push_back(id.GetString());
  }
  if (const base::DictValue* previous = dict->FindDict("previous_committed")) {
    prepared.previous_committed = CommittedFromValue(previous);
    if (!prepared.previous_committed) {
      return {.status = ArcImportStatus::kJournalError};
    }
  }
  if (const base::DictValue* intended = dict->FindDict("intended_committed")) {
    prepared.intended_committed = CommittedFromValue(intended);
    if (!prepared.intended_committed) {
      return {.status = ArcImportStatus::kJournalError};
    }
  }
  if (!IsValidPrepared(prepared)) {
    return {.status = ArcImportStatus::kJournalError};
  }
  return {.state = ArcImportJournalState::kPrepared,
          .prepared = std::move(prepared)};
}

bool WriteArcImportCommittedJournal(const base::FilePath& profile_path,
                                    const ArcImportCommittedState& committed) {
  return IsValidCommitted(committed) &&
         AtomicWriteJournal(profile_path, BuildCommittedJournal(committed));
}

ArcImportCommittedState MakeArcImportCommittedState(
    const std::string& snapshot_hash,
    const std::string& selection_fingerprint,
    const std::string& idempotency_key,
    const ArcImportCommitResult& result) {
  return {.snapshot_hash = snapshot_hash,
          .selection_fingerprint = selection_fingerprint,
          .idempotency_key = idempotency_key,
          .metrics = MetricsForResult(result)};
}

bool WriteArcImportCommittedJournal(const base::FilePath& profile_path,
                                    const std::string& snapshot_hash,
                                    const std::string& selection_fingerprint,
                                    const std::string& idempotency_key,
                                    const ArcImportCommitResult& result) {
  return WriteArcImportCommittedJournal(
      profile_path,
      MakeArcImportCommittedState(snapshot_hash, selection_fingerprint,
                                  idempotency_key, result));
}

bool WriteArcImportPreparedJournal(const base::FilePath& profile_path,
                                   const ArcImportPreparedState& prepared) {
  return IsValidPrepared(prepared) &&
         AtomicWriteJournal(profile_path, BuildPreparedJournal(prepared));
}

bool RestoreArcImportJournalAfterRollback(
    const base::FilePath& profile_path,
    const std::optional<ArcImportCommittedState>& previous_committed) {
  if (!previous_committed) {
    return DeleteJournal(profile_path);
  }
  return IsValidCommitted(*previous_committed) &&
         AtomicWriteJournal(profile_path,
                            BuildCommittedJournal(*previous_committed));
}

bool WriteArcImportJournal(const base::FilePath& profile_path,
                           const std::string& snapshot_hash,
                           const ArcImportCommitResult& result) {
  return WriteArcImportCommittedJournal(profile_path, snapshot_hash,
                                        snapshot_hash, snapshot_hash, result);
}

}  // namespace ahoi::importer::arc
