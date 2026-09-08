// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_SHARED_TAB_TARGET_POLICY_H_
#define AHOI_BROWSER_SESSION_SHARED_TAB_TARGET_POLICY_H_

#include "ahoi/browser/tab_tree/shared_tab_target_policy.h"

namespace ahoi::session {

// Keep the existing Session entry point while Store and Sync use the same pure
// native policy below Session/UI. No duplicate classifier or dependency cycle.
using tab_tree::DescribeNativeSharedTabTarget;
using tab_tree::IsValidSharedPageTarget;
using tab_tree::NativeSharedTabParticipation;
using tab_tree::SelectSharedTabTargetAction;
using tab_tree::SharedTabPresenceMatchesPage;
using tab_tree::SharedTabTarget;
using tab_tree::SharedTabTargetAction;
using tab_tree::SharedTabTargetKind;

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_SHARED_TAB_TARGET_POLICY_H_
