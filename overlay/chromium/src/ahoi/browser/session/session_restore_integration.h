// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_SESSION_RESTORE_INTEGRATION_H_
#define AHOI_BROWSER_SESSION_SESSION_RESTORE_INTEGRATION_H_

#include <map>
#include <optional>
#include <string>

#include "ahoi/browser/session/workspace_session_metadata.h"

class BrowserWindowInterface;
class Profile;

namespace tabs {
class TabInterface;
}

namespace ahoi::session {

// Profile-owned implementation supplied by SessionBridge. Keeping this small
// interface in its own target lets Chromium's SessionRestore implementation
// consume Ahoi metadata without depending back on //chrome/browser/ui through
// the full bridge target.
class WorkspaceSessionMetadataProvider {
 public:
  virtual std::optional<WindowSessionMetadata> GetWindowSessionMetadata(
      const BrowserWindowInterface* browser) const = 0;
  virtual std::optional<TabSessionMetadata> GetTabSessionMetadata(
      const tabs::TabInterface* tab) const = 0;
  virtual bool RestoreWindowSessionMetadata(
      BrowserWindowInterface* browser,
      const WindowSessionMetadata& metadata) = 0;
  virtual bool RestoreTabSessionMetadata(
      tabs::TabInterface* tab,
      const TabSessionMetadata& metadata) = 0;

 protected:
  virtual ~WorkspaceSessionMetadataProvider() = default;
};

// Exactly one provider may be registered for a live regular Profile. The
// caller owns it and must unregister before destruction.
[[nodiscard]] bool RegisterWorkspaceSessionMetadataProvider(
    Profile* profile,
    WorkspaceSessionMetadataProvider* provider);
void UnregisterWorkspaceSessionMetadataProvider(
    Profile* profile,
    WorkspaceSessionMetadataProvider* provider);

// Narrow embedder seams used by Chromium's LiveTabContext, SessionRestore and
// TabRestore paths. They deliberately use the existing extra-data maps rather
// than adding an Ahoi-specific sessions-core concept. Unsupported, malformed,
// OTR and non-normal-window input is ignored without changing Chromium state.
[[nodiscard]] bool PopulateWindowSessionExtraData(
    BrowserWindowInterface* browser,
    std::map<std::string, std::string>* extra_data);
[[nodiscard]] bool PopulateTabSessionExtraData(
    BrowserWindowInterface* browser,
    tabs::TabInterface* tab,
    std::map<std::string, std::string>* extra_data);

[[nodiscard]] bool RestoreWindowSessionExtraData(
    BrowserWindowInterface* browser,
    const std::map<std::string, std::string>& extra_data);
[[nodiscard]] bool RestoreTabSessionExtraData(
    BrowserWindowInterface* browser,
    tabs::TabInterface* tab,
    const std::map<std::string, std::string>& extra_data);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_SESSION_RESTORE_INTEGRATION_H_
