// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/quick_window.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace ahoi::quick_window {

bool IsQuickWindowAccelerator(const ui::Accelerator& accelerator) {
  return accelerator.key_code() == ui::VKEY_SPACE && accelerator.IsAltDown() &&
         !accelerator.IsCmdDown() && !accelerator.IsCtrlDown() &&
         !accelerator.IsShiftDown();
}

bool ShouldActivateQuickWindow(base::TimeTicks last_activation,
                               base::TimeTicks now) {
  return last_activation.is_null() ||
         now - last_activation >= kActivationCooldown;
}

gfx::Rect CalculateQuickWindowBounds(const gfx::Rect& anchor_bounds) {
  const gfx::Point center = anchor_bounds.CenterPoint();
  return gfx::Rect(center.x() - kWidth / 2, center.y() - kHeight / 2, kWidth,
                   kHeight);
}

}  // namespace ahoi::quick_window
