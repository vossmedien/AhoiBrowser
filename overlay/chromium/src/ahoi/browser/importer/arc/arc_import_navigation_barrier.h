// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_NAVIGATION_BARRIER_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_NAVIGATION_BARRIER_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"

namespace tabs {
class TabInterface;
}

namespace ahoi::importer::arc {

// Native SessionService cannot persist a newly opened tab until it has a
// non-initial navigation entry. Wait for commits, not complete page loads or
// arbitrary sleeps. Completion is always posted outside observer callbacks.
class ArcImportNavigationBarrier {
 public:
  ArcImportNavigationBarrier();
  ~ArcImportNavigationBarrier();
  void Start(std::vector<base::WeakPtr<tabs::TabInterface>> tabs,
             base::OnceCallback<void(bool)> callback);

 private:
  class Observer;
  void Check();
  void Finish(bool ready);
  std::vector<base::WeakPtr<tabs::TabInterface>> tabs_;
  std::vector<std::unique_ptr<Observer>> observers_;
  base::OnceCallback<void(bool)> callback_;
  base::OneShotTimer timeout_;
  base::WeakPtrFactory<ArcImportNavigationBarrier> weak_factory_{this};
};

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_NAVIGATION_BARRIER_H_
