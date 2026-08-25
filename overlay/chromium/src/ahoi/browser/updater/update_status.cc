// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/updater/update_status.h"

#include <utility>

namespace ahoi::updater {
namespace {

bool IsAllowedTransition(UpdateStage current, UpdateStage next) {
  if (next == UpdateStage::kUnavailable || next == UpdateStage::kError) {
    return true;
  }
  switch (current) {
    case UpdateStage::kUnavailable:
      return next == UpdateStage::kIdle;
    case UpdateStage::kIdle:
    case UpdateStage::kUpToDate:
    case UpdateStage::kError:
      return next == UpdateStage::kChecking;
    case UpdateStage::kChecking:
      return next == UpdateStage::kUpdateAvailable ||
             next == UpdateStage::kUpToDate;
    case UpdateStage::kUpdateAvailable:
      return next == UpdateStage::kDownloading ||
             next == UpdateStage::kInstalling;
    case UpdateStage::kDownloading:
      return next == UpdateStage::kDownloaded;
    case UpdateStage::kDownloaded:
      return next == UpdateStage::kInstalling;
    case UpdateStage::kInstalling:
      return next == UpdateStage::kRelaunching;
    case UpdateStage::kRelaunching:
      return false;
  }
  return false;
}

}  // namespace

UpdateStatusModel::UpdateStatusModel() = default;
UpdateStatusModel::~UpdateStatusModel() = default;

void UpdateStatusModel::SetUnavailable(std::string reason) {
  status_ = {.stage = UpdateStage::kUnavailable, .error = std::move(reason)};
}

bool UpdateStatusModel::SetReady() {
  return Transition(UpdateStage::kIdle);
}

bool UpdateStatusModel::BeginCheck() {
  return Transition(UpdateStage::kChecking);
}

bool UpdateStatusModel::FoundUpdate(std::string version) {
  if (!Transition(UpdateStage::kUpdateAvailable)) {
    return false;
  }
  status_.version = std::move(version);
  return true;
}

bool UpdateStatusModel::BeginDownload() {
  return Transition(UpdateStage::kDownloading);
}

bool UpdateStatusModel::FinishDownload() {
  return Transition(UpdateStage::kDownloaded);
}

bool UpdateStatusModel::BeginInstall() {
  return Transition(UpdateStage::kInstalling);
}

bool UpdateStatusModel::BeginRelaunch() {
  return Transition(UpdateStage::kRelaunching);
}

bool UpdateStatusModel::MarkUpToDate() {
  return Transition(UpdateStage::kUpToDate);
}

void UpdateStatusModel::Fail(std::string reason) {
  status_.stage = UpdateStage::kError;
  status_.error = std::move(reason);
}

bool UpdateStatusModel::Transition(UpdateStage next) {
  if (!IsAllowedTransition(status_.stage, next)) {
    return false;
  }
  status_.stage = next;
  status_.error.clear();
  if (next == UpdateStage::kIdle || next == UpdateStage::kChecking ||
      next == UpdateStage::kUpToDate) {
    status_.version.clear();
  }
  return true;
}

std::string_view UpdateStageName(UpdateStage stage) {
  switch (stage) {
    case UpdateStage::kUnavailable:
      return "unavailable";
    case UpdateStage::kIdle:
      return "idle";
    case UpdateStage::kChecking:
      return "checking";
    case UpdateStage::kUpdateAvailable:
      return "update-available";
    case UpdateStage::kDownloading:
      return "downloading";
    case UpdateStage::kDownloaded:
      return "downloaded";
    case UpdateStage::kInstalling:
      return "installing";
    case UpdateStage::kRelaunching:
      return "relaunching";
    case UpdateStage::kUpToDate:
      return "up-to-date";
    case UpdateStage::kError:
      return "error";
  }
  return "unknown";
}

}  // namespace ahoi::updater
