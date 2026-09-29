// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_KEYBOARD_SHORTCUTS_H_
#define AHOI_BROWSER_NAVIGATION_KEYBOARD_SHORTCUTS_H_

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "ui/base/accelerators/accelerator.h"

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace ahoi::shortcuts {

// One catalog of Ahoi's keyboard commands. The accelerator registration in
// BrowserView, the menus, the command bar and the settings editor all read
// it, so a changed binding is the same everywhere.
enum class ShortcutCategory {
  kBrowser,
  kTab,
  kWorkspace,
  kSplit,
  kSidebar,
};

struct ShortcutCommand {
  std::string id;
  ShortcutCategory category;
  std::u16string title_de;
  std::u16string title_en;
  std::vector<ui::Accelerator> defaults;
  // False for commands whose key is owned elsewhere (the system-wide Quick
  // Window hotkey, native Undo, Chromium command overrides); they are listed
  // for discovery and conflict checks but cannot be changed yet.
  bool rebindable = true;
};

// Stable ids. Workspace and split pane ids carry their index ("workspace.3").
inline constexpr char kSwitchToLastUsedTab[] = "tab.last-used";
inline constexpr char kPreviousWorkspace[] = "workspace.previous";
inline constexpr char kNextWorkspace[] = "workspace.next";
inline constexpr char kWorkspacePrefix[] = "workspace.";
inline constexpr char kToggleSidebarFloating[] = "sidebar.toggle-floating";
inline constexpr char kToggleSidebarVisibility[] = "sidebar.toggle-visibility";
inline constexpr char kSidebarDiscovery[] = "sidebar.discovery";
inline constexpr char kSidebarUndo[] = "sidebar.undo";
inline constexpr char kSplitPanePrefix[] = "split.focus-pane-";
inline constexpr char kSplitMovePaneLeft[] = "split.move-pane-left";
inline constexpr char kSplitMovePaneRight[] = "split.move-pane-right";
inline constexpr char kSplitShrink[] = "split.ratio-left";
inline constexpr char kSplitGrow[] = "split.ratio-right";
inline constexpr char kSplitCycleLayout[] = "split.cycle-layout";
inline constexpr char kSplitRemove[] = "split.remove";
inline constexpr char kSplitClosePane[] = "split.close-pane";
inline constexpr char kQuickWindow[] = "browser.quick-window";
inline constexpr char kCommandBar[] = "browser.command-bar";
inline constexpr char kSaveTab[] = "tab.save";

const std::vector<ShortcutCommand>& Catalog();
const ShortcutCommand* FindCommand(std::string_view id);
// "workspace.3" -> 2, "split.focus-pane-1" -> 0; nullopt for other ids.
std::optional<size_t> IndexedCommandIndex(std::string_view id,
                                          std::string_view prefix);

// User changes, keyed by command id. An empty list unbinds the command; a
// command without an entry uses its defaults.
inline constexpr char kShortcutBindingsPref[] = "ahoi.shortcuts.bindings";
using Overrides = std::map<std::string, std::vector<ui::Accelerator>,
                           std::less<>>;

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);
Overrides ReadOverrides(const PrefService& prefs);

std::vector<ui::Accelerator> EffectiveAccelerators(const Overrides& overrides,
                                                   std::string_view id);
// The command an accelerator currently triggers, or nullopt.
std::optional<std::string> CommandForAccelerator(
    const Overrides& overrides,
    const ui::Accelerator& accelerator);

enum class ConflictKind {
  kNone,
  // No Command/Control/Option modifier, a bare modifier key, or an Option
  // combination that types a character.
  kInvalid,
  kReservedBySystem,
  kOtherCommand,
  kBrowserCommand,
  kExtension,
  kNotRebindable,
};

struct Conflict {
  ConflictKind kind = ConflictKind::kNone;
  // Set for kOtherCommand.
  std::string other_command_id;

  bool operator==(const Conflict&) const = default;
};

struct ConflictSources {
  // Chromium's own browser accelerators (menu and accelerator table).
  base::RepeatingCallback<bool(const ui::Accelerator&)> is_browser_accelerator;
  // Shortcuts of enabled extensions.
  base::RepeatingCallback<bool(const ui::Accelerator&)>
      is_extension_accelerator;
};

bool IsReservedBySystem(const ui::Accelerator& accelerator);
Conflict CheckBinding(const Overrides& overrides,
                      std::string_view id,
                      const ui::Accelerator& accelerator,
                      const ConflictSources& sources);

// Each change is refused unless CheckBinding reports no conflict; nothing is
// ever overwritten silently. `conflict` receives the reason when not null.
bool SetBinding(PrefService* prefs,
                std::string_view id,
                const ui::Accelerator& accelerator,
                const ConflictSources& sources,
                Conflict* conflict);
bool Unbind(PrefService* prefs, std::string_view id);
bool ResetToDefault(PrefService* prefs, std::string_view id);
void ResetAll(PrefService* prefs);

// The command's title in the browser's language.
std::u16string CommandTitle(const ShortcutCommand& command);
// macOS notation, for example "⌃⌘L" or "⌃`".
std::string ShortcutKeyText(const ui::Accelerator& accelerator);

// While the settings editor records a new key, Ahoi's own shortcuts step
// aside so the pressed key reaches the page (Chromium pre-handles them before
// the renderer). The state expires by itself after 30 seconds.
void SetRecordingActive(bool active);
bool IsRecordingActive();

// Stable storage form, for example "cmd+shift+83" (modifiers, key code).
std::string Serialize(const ui::Accelerator& accelerator);
std::optional<ui::Accelerator> Parse(std::string_view text);

}  // namespace ahoi::shortcuts

#endif  // AHOI_BROWSER_NAVIGATION_KEYBOARD_SHORTCUTS_H_
