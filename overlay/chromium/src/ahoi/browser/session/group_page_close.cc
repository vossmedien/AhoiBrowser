// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/group_page_close.h"

#include <algorithm>
#include <map>
#include <utility>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"

namespace ahoi::session {

// Observes one page of a group. While its answer is pending it is listed in
// `PendingPages()` so the delegate hook can route the answer here. After the
// group finished early (veto elsewhere), unanswered watchers move to
// `Leftovers()` and keep swallowing the late answer, so a late "Leave" never
// closes a tab whose group was already cancelled.
class GroupPageClose::PageWatcher : public content::WebContentsObserver {
 public:
  PageWatcher(GroupPageClose* owner, content::WebContents* contents)
      : content::WebContentsObserver(contents), owner_(owner) {}
  ~PageWatcher() override { Unregister(); }

  void Register() {
    registered_ = true;
    PendingPages()[web_contents()] = this;
  }
  void Unregister() {
    if (!registered_) {
      return;
    }
    registered_ = false;
    auto& pending = PendingPages();
    auto it = pending.find(web_contents());
    if (it != pending.end() && it->second == this) {
      pending.erase(it);
    }
  }
  void Detach() { owner_ = nullptr; }

  void Answer(bool proceed) {
    Unregister();
    answered_ = true;
    ran_before_unload_ = proceed;
    if (owner_) {
      owner_->OnPageAnswered(this, proceed);
    } else {
      DropLeftover(this);
    }
  }
  bool registered() const { return registered_; }
  content::WebContents* contents() const { return web_contents(); }

  static std::map<content::WebContents*, PageWatcher*>& PendingPages() {
    static base::NoDestructor<std::map<content::WebContents*, PageWatcher*>>
        pending;
    return *pending;
  }
  static std::vector<std::unique_ptr<PageWatcher>>& Leftovers() {
    static base::NoDestructor<std::vector<std::unique_ptr<PageWatcher>>>
        leftovers;
    return *leftovers;
  }
  static void DropLeftover(PageWatcher* watcher) {
    auto& leftovers = Leftovers();
    std::erase_if(leftovers,
                  [watcher](const auto& item) { return item.get() == watcher; });
  }

 private:
  // content::WebContentsObserver:
  void WebContentsDestroyed() override { Disappeared(); }
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (handle->IsInPrimaryMainFrame() && handle->HasCommitted() &&
        !handle->IsSameDocument()) {
      Disappeared();
    }
  }
  void Disappeared() {
    // A page that navigated or vanished between question and commit rejects
    // the group, matching the reference behaviour.
    if (answered_) {
      return;
    }
    answered_ = true;
    Unregister();
    Observe(nullptr);
    if (owner_) {
      owner_->OnPageAnswered(this, /*proceed=*/false);
    } else {
      DropLeftover(this);
    }
  }

  raw_ptr<GroupPageClose> owner_;
  bool registered_ = false;
  bool answered_ = false;
  bool ran_before_unload_ = false;
};

GroupPageClose::GroupPageClose(Done done) : done_(std::move(done)) {}

GroupPageClose::~GroupPageClose() {
  if (!finished_) {
    Finish(/*all_agreed=*/false);
  }
}

// static
std::unique_ptr<GroupPageClose> GroupPageClose::Ask(
    std::vector<content::WebContents*> pages,
    Done done,
    bool auto_cancel) {
  auto group = base::WrapUnique(new GroupPageClose(std::move(done)));
  // A page that is already part of another running question cannot be
  // answered twice; treat the overlap as a rejection of this group. Check all
  // pages before registering any, and never report synchronously: callers
  // store the returned group before `done` may run, and they gate new
  // questions on that stored group.
  if (std::ranges::any_of(pages, [](content::WebContents* page) {
        return page && PageWatcher::PendingPages().contains(page);
      })) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&GroupPageClose::Finish,
                                  group->weak_ptr_factory_.GetWeakPtr(),
                                  /*all_agreed=*/false));
    return group;
  }
  std::vector<content::WebContents*> to_dispatch;
  for (content::WebContents* page : pages) {
    if (!page || std::ranges::any_of(group->watchers_, [page](const auto& w) {
          return w->contents() == page;
        })) {
      continue;
    }
    auto watcher = std::make_unique<PageWatcher>(group.get(), page);
    if (page->NeedToFireBeforeUnloadOrUnloadEvents()) {
      watcher->Register();
      ++group->outstanding_;
      to_dispatch.push_back(page);
    }
    group->watchers_.push_back(std::move(watcher));
  }
  if (group->outstanding_ == 0) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&GroupPageClose::Finish,
                                  group->weak_ptr_factory_.GetWeakPtr(),
                                  /*all_agreed=*/true));
    return group;
  }
  // Dispatch only after every page is registered: an immediate answer must
  // not complete the group before later pages were asked.
  for (content::WebContents* page : to_dispatch) {
    page->DispatchBeforeUnload(auto_cancel);
  }
  return group;
}

void GroupPageClose::ClosePages() {
  CHECK(finished_ && agreed_);
  std::vector<std::unique_ptr<PageWatcher>> watchers = std::move(watchers_);
  for (auto& watcher : watchers) {
    if (content::WebContents* contents = watcher->contents()) {
      watcher.reset();
      // Before-unload already ran and agreed (or none exists); ClosePage runs
      // the unload handlers and then closes through the browser delegate.
      contents->ClosePage();
    }
  }
}

// static
bool GroupPageClose::HandleBeforeUnloadFired(content::WebContents* contents,
                                             bool proceed,
                                             bool* proceed_to_fire_unload) {
  auto& pending = PageWatcher::PendingPages();
  auto it = pending.find(contents);
  if (it == pending.end()) {
    return false;
  }
  *proceed_to_fire_unload = false;
  it->second->Answer(proceed);
  return true;
}

void GroupPageClose::OnPageAnswered(PageWatcher* watcher, bool proceed) {
  if (finished_) {
    return;
  }
  if (!proceed) {
    Finish(/*all_agreed=*/false);
    return;
  }
  // Only registered pages answer, and each answers once.
  CHECK_GT(outstanding_, 0u);
  --outstanding_;
  if (outstanding_ == 0) {
    Finish(/*all_agreed=*/true);
  }
}

void GroupPageClose::Finish(bool all_agreed) {
  if (finished_) {
    return;
  }
  finished_ = true;
  agreed_ = all_agreed;
  if (!all_agreed) {
    // Keep watching pages whose prompt is still open so their late answer is
    // swallowed instead of closing the tab.
    for (auto& watcher : watchers_) {
      if (watcher->registered()) {
        watcher->Detach();
        PageWatcher::Leftovers().push_back(std::move(watcher));
      }
    }
    std::erase(watchers_, nullptr);
  }
  if (done_) {
    std::move(done_).Run(all_agreed);
  }
}

}  // namespace ahoi::session
