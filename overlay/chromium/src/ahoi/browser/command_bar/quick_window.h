// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_COMMAND_BAR_QUICK_WINDOW_H_
#define AHOI_BROWSER_COMMAND_BAR_QUICK_WINDOW_H_

#include "base/time/time.h"
#include "ui/gfx/geometry/rect.h"

class Browser;
class Profile;

namespace ui {
class Accelerator;
}

namespace ahoi::quick_window {

inline constexpr int kWidth = 720;
inline constexpr int kHeight = 540;
inline constexpr base::TimeDelta kActivationCooldown = base::Milliseconds(300);

// Product-policy seams kept free of native window state so shortcut routing
// and exact placement remain deterministic in focused tests.
bool IsQuickWindowAccelerator(const ui::Accelerator& accelerator);
bool ShouldActivateQuickWindow(base::TimeTicks last_activation,
                               base::TimeTicks now);
gfx::Rect CalculateQuickWindowBounds(const gfx::Rect& anchor_bounds);

// Creates an ephemeral popup that shares `profile`'s on-device website state.
// Only a regular, non-OTR profile is accepted. `anchor_bounds` is the source
// window bounds, or the primary work area when no source window exists.
Browser* CreateAndShowQuickWindow(Profile* profile,
                                  const gfx::Rect& anchor_bounds);

// Moves the active popup tab, without cloning its WebContents, into the most
// recently active normal window for the same regular profile. A normal window
// is created only when none exists.
bool CanMoveActiveTabToNormalWindow(const Browser* popup_browser);
bool MoveActiveTabToNormalWindow(Browser* popup_browser);

}  // namespace ahoi::quick_window

#endif  // AHOI_BROWSER_COMMAND_BAR_QUICK_WINDOW_H_
