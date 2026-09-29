// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_DECORATIONS_H_
#define AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_DECORATIONS_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/views/view_observer.h"

namespace views {
class View;
}  // namespace views

namespace ahoi {

// The command bar footer (design spec 2026-09-29): a hairline, then the
// keycap hints "↑ ↓ Auswählen  ↵ Öffnen  esc Schließen" in 12 pt secondary
// text. The keys repeat what the list already announces, so the row is
// hidden from accessibility.
std::unique_ptr<views::View> CreateCommandBarKeyHints();

// Draws the spec focus ring (2 pt accent, 2 pt gap) around `shell` only
// while `field`, a descendant of `shell`, holds focus that arrived by
// keyboard traversal. The pointer or programmatic focus the command bar
// opens with draws no ring, so the search field has no permanent outline.
class CommandBarInputFocusRing final : public views::ViewObserver {
 public:
  CommandBarInputFocusRing(views::View* shell, views::View* field);
  CommandBarInputFocusRing(const CommandBarInputFocusRing&) = delete;
  CommandBarInputFocusRing& operator=(const CommandBarInputFocusRing&) =
      delete;
  ~CommandBarInputFocusRing() override;

  // Whether the ring is currently shown.
  bool IsShowingForTesting() const;

  // views::ViewObserver:
  void OnViewFocused(views::View* observed_view) override;
  void OnViewBlurred(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  void Refresh();

  raw_ptr<views::View> shell_;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_DECORATIONS_H_
