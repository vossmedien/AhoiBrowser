// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_LINK_ROUTING_TARGET_CHOOSER_H_
#define AHOI_BROWSER_NAVIGATION_LINK_ROUTING_TARGET_CHOOSER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/uuid.h"
#include "ui/gfx/native_ui_types.h"
#include "url/gurl.h"

namespace ahoi::navigation {

// One choosable Workspace: a normal Workspace of the main Profile or a fully
// separated Workspace. Only the logical Workspace id is returned.
struct LinkRoutingTargetOption {
  base::Uuid workspace_id;
  std::u16string name;
  bool separated = false;
};

struct LinkRoutingTargetChoice {
  base::Uuid workspace_id;
  // "Für diese Website merken" was checked. It is unchecked by default; only
  // this explicit choice may write a rule.
  bool remember = false;
};

// nullopt: the chooser was cancelled or closed without a choice.
using LinkRoutingTargetChosen =
    base::OnceCallback<void(std::optional<LinkRoutingTargetChoice>)>;

// Shows a small window-modal chooser over `parent` for an external link whose
// explicitly chosen Workspace no longer exists (RouteDisposition::
// kNeedsTargetChoice). Lists `options` in order with the first selected and a
// "Für diese Website merken" checkbox. Uses only //ui/views and
// //components/constrained_window, so //ahoi/browser/navigation keeps clear of
// //chrome/browser/ui. Returns false, without running `done`, when nothing
// could be shown (no parent, no options); otherwise `done` runs exactly once.
bool ShowLinkRoutingTargetChooser(gfx::NativeWindow parent,
                                  const GURL& url,
                                  std::vector<LinkRoutingTargetOption> options,
                                  LinkRoutingTargetChosen done);

}  // namespace ahoi::navigation

#endif  // AHOI_BROWSER_NAVIGATION_LINK_ROUTING_TARGET_CHOOSER_H_
