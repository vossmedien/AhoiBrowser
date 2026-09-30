// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/other_profile_media.h"

#include <algorithm>

#include "base/strings/strcat.h"

namespace ahoi::sidebar {

bool PageBelongsToSwitcherEntry(const PageAudioState& page,
                                bool separated_profile,
                                const base::Uuid& workspace_id) {
  return separated_profile || page.workspace_id == workspace_id;
}

bool SwitcherEntryPlaysAudio(const std::vector<PageAudioState>& pages,
                             bool separated_profile,
                             const base::Uuid& workspace_id) {
  return std::ranges::any_of(pages, [&](const PageAudioState& page) {
    return page.audible &&
           PageBelongsToSwitcherEntry(page, separated_profile, workspace_id);
  });
}

std::u16string AudibleSwitcherTitle(const std::u16string& title,
                                    bool german) {
  return base::StrCat(
      {title, german ? u" \u00B7 spielt Audio" : u" \u00B7 playing audio"});
}

std::u16string PauseOtherProfileMediaLabel(const std::u16string& name,
                                           bool german) {
  return german ? base::StrCat({u"Wiedergabe in „", name,
                                u"“ pausieren"})
                : base::StrCat({u"Pause playback in “", name,
                                u"”"});
}

}  // namespace ahoi::sidebar
