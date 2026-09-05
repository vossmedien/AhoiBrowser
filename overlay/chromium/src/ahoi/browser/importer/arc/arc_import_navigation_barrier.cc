// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_navigation_barrier.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"

namespace ahoi::importer::arc {

class ArcImportNavigationBarrier::Observer
    : public content::WebContentsObserver {
 public:
  Observer(content::WebContents* contents,
           base::WeakPtr<ArcImportNavigationBarrier> owner)
      : content::WebContentsObserver(contents), owner_(std::move(owner)) {}
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (owner_ && handle->IsInPrimaryMainFrame()) {
      owner_->Check();
    }
  }
  void WebContentsDestroyed() override {
    if (owner_) {
      owner_->Finish(false);
    }
  }

 private:
  base::WeakPtr<ArcImportNavigationBarrier> owner_;
};

ArcImportNavigationBarrier::ArcImportNavigationBarrier() = default;
ArcImportNavigationBarrier::~ArcImportNavigationBarrier() = default;

void ArcImportNavigationBarrier::Start(
    std::vector<base::WeakPtr<tabs::TabInterface>> tabs,
    base::OnceCallback<void(bool)> callback) {
  CHECK(!callback_);
  CHECK(callback);
  tabs_ = std::move(tabs);
  callback_ = std::move(callback);
  for (const auto& tab : tabs_) {
    if (!tab || !tab->GetContents()) {
      Finish(false);
      return;
    }
    observers_.push_back(std::make_unique<Observer>(
        tab->GetContents(), weak_factory_.GetWeakPtr()));
  }
  timeout_.Start(FROM_HERE, base::Seconds(30),
                 base::BindOnce(&ArcImportNavigationBarrier::Finish,
                                weak_factory_.GetWeakPtr(), false));
  Check();
}

void ArcImportNavigationBarrier::Check() {
  if (!callback_) {
    return;
  }
  for (const auto& tab : tabs_) {
    if (!tab || !tab->GetContents()) {
      Finish(false);
      return;
    }
    const auto* entry =
        tab->GetContents()->GetController().GetLastCommittedEntry();
    if (!entry || entry->IsInitialEntry()) {
      return;
    }
  }
  Finish(true);
}

void ArcImportNavigationBarrier::Finish(bool ready) {
  if (!callback_) {
    return;
  }
  timeout_.Stop();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback_), ready));
}

}  // namespace ahoi::importer::arc
