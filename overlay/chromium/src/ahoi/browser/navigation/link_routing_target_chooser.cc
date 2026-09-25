// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/link_routing_target_chooser.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "components/constrained_window/constrained_window_views.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/views/widget/widget.h"

namespace ahoi::navigation {

namespace {

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kAhoiLinkRoutingTargetComboboxId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kAhoiLinkRoutingRememberCheckboxId);

std::u16string Text(const char* de, const char* en) {
  return base::UTF8ToUTF16(
      base::i18n::GetConfiguredLocale().starts_with("de") ? de : en);
}

// Owned by the DialogModel, so it outlives every button callback.
class ChooserDelegate : public ui::DialogModelDelegate {
 public:
  ChooserDelegate(std::vector<LinkRoutingTargetOption> options,
                  LinkRoutingTargetChosen done)
      : options_(std::move(options)), done_(std::move(done)) {}
  ~ChooserDelegate() override { Finish(std::nullopt); }

  void OnAccept() {
    ui::DialogModelCombobox* combobox =
        dialog_model()->GetComboboxByUniqueId(kAhoiLinkRoutingTargetComboboxId);
    ui::DialogModelCheckbox* checkbox = dialog_model()->GetCheckboxByUniqueId(
        kAhoiLinkRoutingRememberCheckboxId);
    const size_t index = combobox ? combobox->selected_index() : 0;
    if (index >= options_.size()) {
      Finish(std::nullopt);
      return;
    }
    Finish(LinkRoutingTargetChoice{
        .workspace_id = options_[index].workspace_id,
        .remember = checkbox && checkbox->is_checked()});
  }

  void OnCancel() { Finish(std::nullopt); }

 private:
  void Finish(std::optional<LinkRoutingTargetChoice> choice) {
    if (done_) {
      std::move(done_).Run(std::move(choice));
    }
  }

  const std::vector<LinkRoutingTargetOption> options_;
  LinkRoutingTargetChosen done_;
};

}  // namespace

bool ShowLinkRoutingTargetChooser(gfx::NativeWindow parent,
                                  const GURL& url,
                                  std::vector<LinkRoutingTargetOption> options,
                                  LinkRoutingTargetChosen done) {
  if (!parent || options.empty() || !done) {
    return false;
  }
  std::vector<ui::SimpleComboboxModel::Item> items;
  items.reserve(options.size());
  for (const LinkRoutingTargetOption& option : options) {
    items.emplace_back(option.separated
                           ? base::StrCat({option.name, u" ",
                                           Text("(vollständig getrennt)",
                                                "(fully separated)")})
                           : option.name);
  }
  const std::u16string host = base::UTF8ToUTF16(url.host());

  auto delegate =
      std::make_unique<ChooserDelegate>(std::move(options), std::move(done));
  ChooserDelegate* const delegate_ptr = delegate.get();
  ui::DialogModel::Builder builder(std::move(delegate));
  builder
      .SetTitle(Text("Workspace für diesen Link wählen",
                     "Choose a Workspace for this link"))
      .AddParagraph(ui::DialogModelLabel(base::StrCat(
          {Text("Der für ", "The Workspace chosen for "), host,
           Text(" gewählte Workspace ist nicht mehr verfügbar. Wo soll der "
                "Link geöffnet werden?",
                " is no longer available. Where should the link open?")})))
      .AddCombobox(kAhoiLinkRoutingTargetComboboxId,
                   Text("Workspace", "Workspace"),
                   std::make_unique<ui::SimpleComboboxModel>(std::move(items)))
      .AddCheckbox(kAhoiLinkRoutingRememberCheckboxId,
                   ui::DialogModelLabel(Text("Für diese Website merken",
                                             "Remember for this website")))
      .AddOkButton(
          base::BindOnce(&ChooserDelegate::OnAccept,
                         base::Unretained(delegate_ptr)),
          ui::DialogModel::Button::Params().SetLabel(Text("Öffnen", "Open")))
      .AddCancelButton(base::BindOnce(&ChooserDelegate::OnCancel,
                                      base::Unretained(delegate_ptr)),
                       ui::DialogModel::Button::Params().SetLabel(
                           Text("Nicht öffnen", "Don't open")))
      .SetCloseActionCallback(base::BindOnce(&ChooserDelegate::OnCancel,
                                             base::Unretained(delegate_ptr)))
      .SetInitiallyFocusedField(kAhoiLinkRoutingTargetComboboxId);
  // Destroying the model without a button press runs `done` with nullopt from
  // the delegate's destructor, so `done` runs exactly once.
  constrained_window::ShowBrowserModal(builder.Build(), parent);
  return true;
}

}  // namespace ahoi::navigation
