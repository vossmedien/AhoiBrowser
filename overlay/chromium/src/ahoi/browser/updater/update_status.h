// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UPDATER_UPDATE_STATUS_H_
#define AHOI_BROWSER_UPDATER_UPDATE_STATUS_H_

#include <optional>
#include <string>
#include <string_view>

namespace ahoi::updater {

enum class UpdateStage {
  kUnavailable,
  kIdle,
  kChecking,
  kUpdateAvailable,
  kDownloading,
  kDownloaded,
  kInstalling,
  kRelaunching,
  kUpToDate,
  kError,
};

struct UpdateStatus {
  UpdateStage stage = UpdateStage::kUnavailable;
  std::string version;
  std::string error;
};

class UpdateStatusModel {
 public:
  UpdateStatusModel();
  ~UpdateStatusModel();

  const UpdateStatus& status() const { return status_; }
  void SetUnavailable(std::string reason);
  bool SetReady();
  bool BeginCheck();
  bool FoundUpdate(std::string version);
  bool BeginDownload();
  bool FinishDownload();
  bool BeginInstall();
  bool BeginRelaunch();
  bool MarkUpToDate();
  void Fail(std::string reason);

 private:
  bool Transition(UpdateStage next);

  UpdateStatus status_;
};

std::string_view UpdateStageName(UpdateStage stage);

}  // namespace ahoi::updater

#endif  // AHOI_BROWSER_UPDATER_UPDATE_STATUS_H_
