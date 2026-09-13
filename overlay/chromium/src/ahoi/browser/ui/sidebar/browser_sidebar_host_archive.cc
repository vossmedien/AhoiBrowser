// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/rtl.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {
namespace {
std::u16string Text(std::u16string_view german, std::u16string_view english) {
  return std::u16string(base::StartsWith(base::i18n::GetConfiguredLocale(),
                                         "de",
                                         base::CompareCase::INSENSITIVE_ASCII)
                            ? german
                            : english);
}

// A bounded-height native dialog, not another sidebar section or data store.
// Search operates only on this profile's already retained portable metadata.
class ArchiveSearchView final : public views::View,
                                public views::TextfieldController {
  METADATA_HEADER(ArchiveSearchView, views::View)
 public:
  using Action =
      base::RepeatingCallback<void(sync::TabArchiveEntryRecord, bool)>;
  ArchiveSearchView(std::vector<sync::TabArchiveEntryRecord> entries,
                    Action action)
      : entries_(std::move(entries)), action_(std::move(action)) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 10));
    search_ = AddChildView(std::make_unique<views::Textfield>());
    search_->SetPlaceholderText(
        Text(u"Titel, Adresse, Grund oder Zeitpunkt suchen",
             u"Search title, address, reason or time"));
    search_->GetViewAccessibility().SetName(
        Text(u"Archiv durchsuchen", u"Search archive"));
    search_->set_controller(this);
    auto* explanation = AddChildView(std::make_unique<views::Label>(
        Text(u"Archivierte Tabs bleiben wiederherstellbar. Wiederherstellen "
             u"lädt keine Seiten automatisch.",
             u"Archived tabs remain recoverable. Restoring does not load pages "
             u"automatically.")));
    explanation->SetMultiLine(true);
    explanation->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    auto* scroll = AddChildView(std::make_unique<views::ScrollView>());
    scroll->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    scroll->ClipHeightTo(240, 360);
    auto contents = std::make_unique<views::View>();
    contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 12));
    rows_ = scroll->SetContents(std::move(contents));
    Rebuild();
  }
  views::Textfield* search_field() const { return search_; }
  void ContentsChanged(views::Textfield*, const std::u16string&) override {
    Rebuild();
  }

 private:
  void Rebuild() {
    rows_->RemoveAllChildViews();
    const auto query = base::i18n::ToLower(search_->GetText());
    size_t count = 0;
    for (const auto& entry : entries_) {
      std::u16string title, searchable;
      for (const auto& page : entry.snapshot.pages) {
        if (!title.empty())
          title += u" · ";
        title += base::UTF8ToUTF16(page.title);
        searchable +=
            base::UTF8ToUTF16(page.title + " " + page.target.url) + u" ";
      }
      std::u16string detail = entry.snapshot.split
                                  ? Text(u"Split · ", u"Split · ")
                                  : std::u16string();
      detail += entry.reason == sync::SharedArchiveReason::kAutomatic
                    ? Text(u"Automatisch · ", u"Automatic · ")
                    : Text(u"Manuell · ", u"Manual · ");
      detail += base::TimeFormatShortDateAndTime(entry.archived_at);
      searchable += detail;
      if (!query.empty() &&
          base::i18n::ToLower(searchable).find(query) == std::u16string::npos)
        continue;
      ++count;
      auto row = std::make_unique<views::View>();
      row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
      auto* heading = row->AddChildView(std::make_unique<views::Label>(title));
      heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      heading->SetMultiLine(true);
      heading->SetMaxLines(2);
      auto* metadata =
          row->AddChildView(std::make_unique<views::Label>(detail));
      metadata->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      auto buttons = std::make_unique<views::View>();
      buttons->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
      auto add_action = [&](std::u16string label, bool remove) {
        auto button = std::make_unique<views::LabelButton>(
            base::BindRepeating(action_, entry, remove), label);
        button->GetViewAccessibility().SetName(label + u": " + title);
        buttons->AddChildView(std::move(button));
      };
      add_action(Text(u"Wiederherstellen …", u"Restore…"), false);
      add_action(Text(u"Endgültig löschen …", u"Delete permanently…"), true);
      row->AddChildView(std::move(buttons));
      rows_->AddChildView(std::move(row));
    }
    if (!count)
      rows_->AddChildView(std::make_unique<views::Label>(
          entries_.empty()
              ? Text(u"Das Archiv ist leer.", u"The archive is empty.")
              : Text(u"Keine passenden Einträge.", u"No matching entries.")));
    rows_->InvalidateLayout();
  }
  const std::vector<sync::TabArchiveEntryRecord> entries_;
  const Action action_;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::View> rows_ = nullptr;
};
BEGIN_METADATA(ArchiveSearchView)
END_METADATA
}  // namespace

void BrowserSidebarHostView::ShowArchiveSearch() {
  if (!GetWidget() || archive_search_widget_ || structure_dialog_widget_ ||
      context_menu_scope_ != ContextMenuScope::kNone)
    return;
  auto contents = std::make_unique<ArchiveSearchView>(
      session_bridge_->GetArchivedPages(),
      base::BindRepeating(&BrowserSidebarHostView::HandleArchiveSearchAction,
                          weak_ptr_factory_.GetWeakPtr()));
  auto* search = contents->search_field();
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      workspace_button_, views::BubbleBorder::TOP_LEFT,
      views::BubbleBorder::DIALOG_SHADOW, true);
  delegate->SetTitle(StructureText(u"Archiv", u"Archive"));
  delegate->set_fixed_width(560);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
  delegate->SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetButtonLabel(ui::mojom::DialogButton::kCancel,
                           StructureText(u"Schließen", u"Close"));
  delegate->SetInitiallyFocusedView(search);
  delegate->SetContentsView(std::move(contents));
  auto widget = views::BubbleDialogDelegate::CreateBubble(
      delegate.get(),
      base::IgnoreArgs<views::Widget::ClosedReason>(
          base::BindOnce(&BrowserSidebarHostView::OnArchiveSearchClosed,
                         weak_ptr_factory_.GetWeakPtr())));
  if (!widget)
    return;
  archive_search_delegate_ = std::move(delegate);
  archive_search_widget_ = std::move(widget);
  archive_search_widget_->Show();
}

void BrowserSidebarHostView::OnArchiveSearchClosed() {
  archive_search_widget_.reset();
  archive_search_delegate_.reset();
}

void BrowserSidebarHostView::HandleArchiveSearchAction(
    sync::TabArchiveEntryRecord expected,
    bool remove) {
  // Finish the current button dispatch before closing/destroying its row.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<BrowserSidebarHostView> host,
             sync::TabArchiveEntryRecord expected, bool remove) {
            if (!host)
              return;
            host->OnArchiveSearchClosed();
            if (!host)
              return;
            if (remove)
              host->ConfirmArchiveDelete(std::move(expected));
            else
              host->ShowArchiveRestoreMenu(expected.id);
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(expected), remove));
}

void BrowserSidebarHostView::ConfirmArchiveDelete(
    sync::TabArchiveEntryRecord expected) {
  if (!GetWidget() || structure_dialog_widget_ ||
      expected.snapshot.pages.empty())
    return;
  auto dialog =
      ui::DialogModel::Builder()
          .SetTitle(StructureText(u"Archiveintrag endgültig löschen?",
                                  u"Permanently delete archive entry?"))
          .AddParagraph(ui::DialogModelLabel(
              base::UTF8ToUTF16(expected.snapshot.pages.front().title)))
          .AddParagraph(ui::DialogModelLabel(StructureText(
              u"Dieser Eintrag einschließlich aller Split-Seiten kann danach "
              u"nicht wiederhergestellt werden. Offene Seiten werden nicht "
              u"geschlossen. Die Löschung wird bei erlaubtem Sync auf deine "
              u"anderen Geräte übertragen.",
              u"This entry, including all split pages, can no longer be "
              u"restored. Open pages will not be closed. The deletion will "
              u"sync to your other devices when sync is allowed.")))
          .AddOkButton(
              base::BindOnce(
                  [](base::WeakPtr<BrowserSidebarHostView> host,
                     sync::TabArchiveEntryRecord expected) {
                    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                        FROM_HERE,
                        base::BindOnce(
                            [](base::WeakPtr<BrowserSidebarHostView> host,
                               sync::TabArchiveEntryRecord expected) {
                              if (host)
                                host->session_bridge_->DeleteArchivedPages(
                                    std::move(expected),
                                    base::BindOnce(
                                        [](base::WeakPtr<BrowserSidebarHostView>
                                               host,
                                           bool ok) {
                                          if (!host)
                                            return;
                                          if (ok) {
                                            host->ScheduleRuntimePresentationRefresh();
                                            host->ShowArchiveSearch();
                                          } else
                                            host->ShowStructureNotice(
                                                StructureText(u"Nicht gelöscht",
                                                              u"Not deleted"),
                                                StructureText(
                                                    u"Der Eintrag oder seine "
                                                    u"Seiten haben sich "
                                                    u"geändert oder sind noch "
                                                    u"geöffnet. Öffne das "
                                                    u"Archiv erneut und prüfe "
                                                    u"den aktuellen Stand, "
                                                    u"bevor du erneut "
                                                    u"bestätigst.",
                                                    u"The entry or its pages "
                                                    u"changed or are still "
                                                    u"open. Reopen the archive "
                                                    u"and review the current "
                                                    u"state before confirming "
                                                    u"again."));
                                        },
                                        host));
                            },
                            host, std::move(expected)));
                  },
                  weak_ptr_factory_.GetWeakPtr(), std::move(expected)),
              ui::DialogModel::Button::Params().SetLabel(
                  StructureText(u"Endgültig löschen", u"Delete permanently")))
          .AddCancelButton(base::DoNothing(),
                           ui::DialogModel::Button::Params().SetLabel(
                               StructureText(u"Abbrechen", u"Cancel")))
          .Build();
  auto delegate = std::make_unique<views::BubbleDialogModelHost>(
      std::move(dialog), workspace_button_, views::BubbleBorder::TOP_LEFT);
  delegate->SetDefaultButton(
      static_cast<int>(ui::mojom::DialogButton::kCancel));
  structure_dialog_widget_ = views::BubbleDialogDelegate::CreateBubble(
      delegate.get(),
      base::IgnoreArgs<views::Widget::ClosedReason>(
          base::BindOnce(&BrowserSidebarHostView::OnStructureDialogClosed,
                         weak_ptr_factory_.GetWeakPtr())));
  if (structure_dialog_widget_) {
    delegate.release();
    structure_dialog_widget_->Show();
  }
}
}  // namespace ahoi::sidebar
