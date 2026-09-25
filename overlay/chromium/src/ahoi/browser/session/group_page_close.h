// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_GROUP_PAGE_CLOSE_H_
#define AHOI_BROWSER_SESSION_GROUP_PAGE_CLOSE_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"

namespace content {
class WebContents;
}

namespace ahoi::session {

// Two-phase close for a group of tab pages (archive, "close all temporary
// tabs", Workspace deletion, split close). Phase one asks every page's
// before-unload handler first without closing anything; the result is true
// only if every page agreed and none navigated away or disappeared meanwhile.
// Phase two is the caller's: commit the semantic change, then call
// `ClosePages()`, which closes agreed pages without asking them again.
//
// Chromium only offers this all-or-nothing behaviour for whole windows
// (`UnloadController`). Ordered patch `0055` routes
// `BrowserWebContentsDelegate::BeforeUnloadFired` through
// `HandleBeforeUnloadFired()` so an agreeing page is not closed early.
class GroupPageClose {
 public:
  using Done = base::OnceCallback<void(bool all_agreed)>;

  GroupPageClose(const GroupPageClose&) = delete;
  GroupPageClose& operator=(const GroupPageClose&) = delete;
  ~GroupPageClose();

  // Starts phase one. `done` runs exactly once, asynchronously if any page
  // shows a before-unload prompt. The returned object must stay alive until
  // `done` ran; destroying it earlier reports false.
  // With `auto_cancel` a page that wants to prompt answers "no" without any
  // dialog (used by automatic archiving, which must never ask).
  static std::unique_ptr<GroupPageClose> Ask(
      std::vector<content::WebContents*> pages,
      Done done,
      bool auto_cancel = false);

  // Phase two. Closes every page that is still alive: pages that ran their
  // before-unload handler proceed straight to unload; the rest take the
  // normal close path. Only valid after `done(true)`.
  void ClosePages();

  // Called from the patched `BrowserWebContentsDelegate::BeforeUnloadFired`.
  // Returns true when `contents` belongs to a running group question; then
  // `*proceed_to_fire_unload` is false so the page is not closed yet.
  static bool HandleBeforeUnloadFired(content::WebContents* contents,
                                      bool proceed,
                                      bool* proceed_to_fire_unload);

 private:
  class PageWatcher;

  explicit GroupPageClose(Done done);
  void OnPageAnswered(PageWatcher* watcher, bool proceed);
  void Finish(bool all_agreed);

  std::vector<std::unique_ptr<PageWatcher>> watchers_;
  size_t outstanding_ = 0;
  bool finished_ = false;
  bool agreed_ = false;
  Done done_;
  base::WeakPtrFactory<GroupPageClose> weak_ptr_factory_{this};
};

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_GROUP_PAGE_CLOSE_H_
