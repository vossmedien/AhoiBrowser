// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_OTHER_PROFILE_MEDIA_CHROMIUM_H_
#define AHOI_BROWSER_UI_SIDEBAR_OTHER_PROFILE_MEDIA_CHROMIUM_H_

#include <cstddef>
#include <string>

#include "base/uuid.h"

namespace ahoi::sidebar {

// WS-ISO-18: whether the switcher entry of another Profile (the main one for
// an empty `profile_dir`) plays audio in any of its windows, hidden ones
// included. A Profile that is not loaded plays nothing.
bool OtherProfileWorkspacePlaysAudio(const std::string& profile_dir,
                                     const base::Uuid& workspace_id);

// Pauses that entry's audible pages through their media session, and mutes
// a page that still plays without one. Returns how many pages it reached.
size_t PauseOtherProfileWorkspaceMedia(const std::string& profile_dir,
                                       const base::Uuid& workspace_id);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_OTHER_PROFILE_MEDIA_CHROMIUM_H_
