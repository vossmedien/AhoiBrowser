// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/media/tab_activity_state.h"

namespace ahoi {

bool IsAhoiCaptureActivityAlert(tabs::TabAlert alert) {
  switch (alert) {
    case tabs::TabAlert::kMediaRecording:
    case tabs::TabAlert::kAudioRecording:
    case tabs::TabAlert::kVideoRecording:
    case tabs::TabAlert::kTabCapturing:
    case tabs::TabAlert::kDesktopCapturing:
      return true;
    case tabs::TabAlert::kAudioPlaying:
    case tabs::TabAlert::kAudioMuting:
    case tabs::TabAlert::kBluetoothConnected:
    case tabs::TabAlert::kBluetoothScanActive:
    case tabs::TabAlert::kUsbConnected:
    case tabs::TabAlert::kHidConnected:
    case tabs::TabAlert::kSerialConnected:
    case tabs::TabAlert::kPipPlaying:
    case tabs::TabAlert::kVrPresentingInHeadset:
    case tabs::TabAlert::kGlicAccessing:
    case tabs::TabAlert::kGlicSharing:
    case tabs::TabAlert::kActorAccessing:
    case tabs::TabAlert::kActorWaitingOnUser:
      return false;
  }
}

// static
AhoiTabActivityState AhoiTabActivityState::FromChromiumAlert(
    std::optional<tabs::TabAlert> alert) {
  return {.primary_activity =
              alert.has_value() && IsAhoiCaptureActivityAlert(*alert)
                  ? alert
                  : std::nullopt};
}

}  // namespace ahoi
