// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_OTHER_PROFILE_MEDIA_H_
#define AHOI_BROWSER_UI_SIDEBAR_OTHER_PROFILE_MEDIA_H_

#include <optional>
#include <string>
#include <vector>

#include "base/uuid.h"

namespace ahoi::sidebar {

// ADR 0011 WS-ISO-18 (handoff 016 H3): a hand-over hides the source window,
// and its pages keep playing. The shared switcher marks every Workspace of
// another Profile that plays audio and offers to pause it there, without
// switching back. Pure part; the Chromium lookup is in
// other_profile_media_chromium.cc.

struct PageAudioState {
  // The page's Workspace; empty for a page the tree does not track.
  std::optional<base::Uuid> workspace_id;
  bool audible = false;
};

// Whether a switcher entry of another Profile plays audio. A fully separated
// Workspace is its Profile's only Workspace, so every audible page there
// counts; in the main Profile only the pages of `workspace_id` do.
bool SwitcherEntryPlaysAudio(const std::vector<PageAudioState>& pages,
                             bool separated_profile,
                             const base::Uuid& workspace_id);

// Whether one page belongs to the entry, by the same rule.
bool PageBelongsToSwitcherEntry(const PageAudioState& page,
                                bool separated_profile,
                                const base::Uuid& workspace_id);

// "<title> · spielt Audio" for the switcher item.
std::u16string AudibleSwitcherTitle(const std::u16string& title, bool german);
// "Wiedergabe in „<name>“ pausieren" for the item below the list.
std::u16string PauseOtherProfileMediaLabel(const std::u16string& name,
                                           bool german);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_OTHER_PROFILE_MEDIA_H_
