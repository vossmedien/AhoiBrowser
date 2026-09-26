// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_COMMAND_BAR_COMMAND_EXECUTION_ADAPTER_INTERNAL_H_
#define AHOI_BROWSER_COMMAND_BAR_COMMAND_EXECUTION_ADAPTER_INTERNAL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_toolkit_types.h"
#include "ahoi/browser/navigation/command_service.h"
#include "base/uuid.h"

namespace ahoi::internal {

// Deliberately outside Chromium's positive command-id range. Production code
// handles this reviewed Ahoi action explicitly instead of forwarding it to
// chrome::ExecuteCommand().
inline constexpr int kOpenInNormalWindowCommand = -1;
inline constexpr int kOpenPrivacyModeCommand = -2;
inline constexpr int kSwitchHttpAuthAccountCommand = -3;
inline constexpr int kForgetHttpAuthRealmCommand = -4;
inline constexpr int kManageHttpAuthCredentialsCommand = -5;
inline constexpr int kCopyActivePageLinkCommand = -6;
inline constexpr int kCopyActivePageMarkdownLinkCommand = -7;
inline constexpr int kOpenActivePageInReadingModeCommand = -8;

// Command-bar ids of shortcut catalog commands.
inline constexpr char kShortcutCommandPrefix[] = "shortcut.";
// Command-bar ids of "In Workspace verschieben" (ADR 0012 section 2): the
// prefix plus the target Workspace's lowercase UUID.
inline constexpr char kMoveToWorkspaceCommandPrefix[] = "move-to-workspace.";

struct MoveToWorkspaceTarget {
  base::Uuid id;
  std::u16string name;
};

// One browser-command item per target, findable by the command's words in
// both languages and by the Workspace's name.
std::vector<CommandItem> BuildMoveToWorkspaceCommands(
    const std::vector<MoveToWorkspaceTarget>& targets,
    bool german);
// The target Workspace id of a move command's stable id, if it is one.
std::optional<std::string_view> GetMoveToWorkspaceTarget(
    std::string_view stable_id);

// Converts only the deliberately small, reviewed command-bar allowlist into
// Chromium command identifiers. Keeping this in the testable core prevents a
// string supplied by an index publisher from becoming an arbitrary browser
// command.
std::optional<int> GetAllowlistedBrowserCommand(std::string_view stable_id);
std::optional<DeveloperAction> GetAllowlistedDeveloperAction(
    std::string_view stable_id);

}  // namespace ahoi::internal

#endif  // AHOI_BROWSER_COMMAND_BAR_COMMAND_EXECUTION_ADAPTER_INTERNAL_H_
