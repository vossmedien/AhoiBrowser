// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_VIEW_H_
#define AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_VIEW_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/command_bar/command_bar_types.h"
#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace ui {
class DropTargetEvent;
class KeyEvent;
class LayerTreeOwner;
}  // namespace ui

namespace views {
class Textfield;
}

class PrefService;

namespace ahoi {

class CommandBarResultRow;

// Content view hosted by a separate BubbleDialogDelegate. Keeping the delegate
// and View separate follows M151's supported ownership model and lets the
// controller own the bubble Widget explicitly.
class CommandBarView : public views::View, public views::TextfieldController {
 public:
  using SuggestionsCallback =
      base::RepeatingCallback<std::vector<CommandBarSuggestion>(
          std::u16string_view)>;
  using ExecuteCallback =
      base::RepeatingCallback<bool(const CommandBarSuggestion&,
                                   std::u16string_view)>;

  CommandBarView(CommandBarDisposition disposition,
                 std::u16string placeholder,
                 SuggestionsCallback suggestions_callback,
                 ExecuteCallback execute_callback,
                 base::RepeatingClosure close_callback,
                 base::OnceClosure destroyed_callback,
                 PrefService* prefs = nullptr);
  CommandBarView(const CommandBarView&) = delete;
  CommandBarView& operator=(const CommandBarView&) = delete;
  ~CommandBarView() override;

  void SetInitialQuery(std::u16string query, bool prefer_input_fallback);
  void FocusInput();
  void ReapplyAppearance();
  // Re-runs the current local query after an asynchronous source (history or
  // favicon cache) has published fresher data.
  void RefreshSuggestions();
  CommandBarDisposition disposition() const { return disposition_; }

  // Test surface intentionally exposes semantic state rather than child-order
  // implementation details.
  size_t suggestion_count_for_testing() const { return suggestions_.size(); }
  std::optional<size_t> selected_index_for_testing() const {
    return selected_index_;
  }
  views::Textfield* textfield_for_testing() { return textfield_; }
  views::View* results_view_for_testing() { return results_view_; }
  views::View* row_for_testing(size_t index) const;
  bool row_selected_for_testing(size_t index) const;
  bool HandleKeyEventForTesting(const ui::KeyEvent& event);
  bool HandleResultKeyEventForTesting(size_t index, const ui::KeyEvent& event);

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  void OnBeforePaste(views::Textfield* sender,
                     base::OnceCallback<void(std::optional<std::u16string>)>
                         callback) override;
  views::View::DropCallback CreateDropCallback(
      const ui::DropTargetEvent& event) override;

 private:
  void OnAppearanceChanged(const appearance::GlassPolicy& policy);

  void RebuildSuggestions(bool prefer_input_fallback);
  void SelectIndex(std::optional<size_t> index, bool request_focus);
  bool MoveSelection(int delta, bool request_focus);
  bool AcceptSelection();
  void ScheduleAcceptSelection();
  bool HandleResultKeyEvent(size_t index, const ui::KeyEvent& event);
  void OnResultHovered(size_t index);
  void PerformDrop(const ui::DropTargetEvent& event,
                   ui::mojom::DragOperation& output_drag_op,
                   std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner);
  void CloseCommandBar();

  const CommandBarDisposition disposition_;
  SuggestionsCallback suggestions_callback_;
  ExecuteCallback execute_callback_;
  base::RepeatingClosure close_callback_;
  base::OnceClosure destroyed_callback_;

  raw_ptr<views::Textfield> textfield_ = nullptr;
  raw_ptr<views::View> results_view_ = nullptr;
  std::vector<raw_ptr<CommandBarResultRow>> rows_;
  std::vector<CommandBarSuggestion> suggestions_;
  std::optional<size_t> selected_index_;
  std::unique_ptr<appearance::AppearanceRuntimeSignalSource>
      appearance_signal_source_;
  base::WeakPtrFactory<CommandBarView> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_VIEW_H_
