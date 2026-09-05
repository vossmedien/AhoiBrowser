// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_bookmark_sync_control.h"

#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/profile_sync_service_factory.h"
#include "ahoi/browser/ui/sidebar/sidebar_bookmark_button.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
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

class BookmarkSyncControl final : public views::View {
  METADATA_HEADER(BookmarkSyncControl, views::View)
 public:
  explicit BookmarkSyncControl(Profile* profile) : profile_(profile) {
    SetLayoutManager(std::make_unique<views::FillLayout>());
    button_ = AddChildView(std::make_unique<SidebarBookmarkButton>(
        base::BindRepeating(&BookmarkSyncControl::ShowConsent,
                            weak_ptr_factory_.GetWeakPtr()),
        std::u16string(), ui::ImageModel(), std::u16string(), false));
    pref_observer_.Init(profile->GetPrefs());
    for (const auto* pref :
         {sync::kBookmarkSyncEnabledPref, sync::kSyncEnabledPref}) {
      pref_observer_.Add(pref,
                         base::BindRepeating(&BookmarkSyncControl::Refresh,
                                             weak_ptr_factory_.GetWeakPtr()));
    }
    if (auto* service = SyncService()) {
      status_subscription_ = service->ObserveBookmarkSync(base::BindRepeating(
          &BookmarkSyncControl::Refresh, weak_ptr_factory_.GetWeakPtr()));
    }
    Refresh();
  }

  ~BookmarkSyncControl() override {
    weak_ptr_factory_.InvalidateWeakPtrs();
    bubble_widget_.reset();
  }

 private:
  sync::ProfileSyncService* SyncService() const {
    return sync::ProfileSyncServiceFactory::GetForProfile(profile_);
  }
  std::u16string IssueExplanation() const {
    const auto* service = SyncService();
    if (!service) {
      return {};
    }
    switch (service->bookmark_sync_issue()) {
      case sync::ProfileSyncService::BookmarkSyncIssue::kNone:
        return {};
      case sync::ProfileSyncService::BookmarkSyncIssue::kUnsupportedLocalData:
        return Text(
            u"Der Lesezeichen-Abgleich pausiert: Ein lokaler Eintrag enthält "
            u"Anmeldedaten in seiner Adresse oder nicht übertragbare "
            u"Metadaten. "
            u"Prüfe die Einträge in der Lesezeichenverwaltung. Nach einer "
            u"Korrektur wird erneut abgeglichen. Lokale Einträge bleiben "
            u"unverändert; andere Sync-Kategorien sind nicht angehalten.",
            u"Bookmark reconciliation is paused: a local entry contains "
            u"credentials in its address or unsupported metadata. Check the "
            u"entries in Bookmark Manager. Editing them retries "
            u"reconciliation. "
            u"Local entries remain unchanged; other sync categories are not "
            u"paused.");
      case sync::ProfileSyncService::BookmarkSyncIssue::kReconciliationFailed:
        return Text(
            u"Der lokale Lesezeichen-Abgleich konnte nicht bestätigt werden. "
            u"Noch nicht gesicherte lokale Änderungen werden nicht durch den "
            u"Sync-Stand ersetzt. Beim nächsten Abgleich wird erneut versucht; "
            u"dies ist noch kein bestätigter Sync-Erfolg.",
            u"Local bookmark reconciliation could not be confirmed. Unsaved "
            u"local changes will not be replaced by the synced state. The next "
            u"reconciliation retries; sync success has not been confirmed.");
      case sync::ProfileSyncService::BookmarkSyncIssue::kAuthorizationChanged:
        return Text(
            u"Konto oder Sync-Freigabe haben sich geändert. Der Lesezeichen-"
            u"Abgleich wartet auf den aktuellen Zustand; ältere Antworten "
            u"werden nicht mehr angewendet. Lokale Lesezeichen bleiben "
            u"nutzbar.",
            u"The account or sync approval changed. Bookmark reconciliation "
            u"is waiting for the current state; older replies will not be "
            u"applied. Local bookmarks remain usable.");
    }
  }
  bool approved() const {
    return profile_->GetPrefs()->GetBoolean(sync::kBookmarkSyncEnabledPref);
  }
  void Refresh() {
    const bool enabled = approved();
    const bool global =
        profile_->GetPrefs()->GetBoolean(sync::kSyncEnabledPref);
    const auto* service = SyncService();
    const bool ready =
        service && service->transport_status().provider_available &&
        !service->transport_status().account_transition_pending &&
        !service->transport_status().zone_recovery_pending;
    const bool issue = !IssueExplanation().empty();
    auto tooltip =
        enabled
            ? Text(u"Lesezeichen-Sync: freigegeben", u"Bookmark sync: approved")
            : Text(u"Lesezeichen-Sync einrichten", u"Set up bookmark sync");
    if (enabled && issue) {
      tooltip = Text(u"Lesezeichen-Sync: Abgleich pausiert",
                     u"Bookmark sync: reconciliation paused");
    } else if (enabled && (!global || !ready)) {
      tooltip = Text(u"Lesezeichen freigegeben; Ahoi-Sync noch nicht bereit",
                     u"Bookmarks approved; Ahoi Sync is not ready");
    }
    button_->UpdatePresentation(
        {},
        ui::ImageModel::FromVectorIcon(
            enabled ? vector_icons::kSyncIcon : vector_icons::kSyncDisabledIcon,
            enabled && global && ready && !issue ? visual_style::kAccent
                                                 : visual_style::kMutedText,
            visual_style::kSidebarIconSize),
        tooltip);
  }
  void ShowConsent() {
    if (!GetWidget() || !IsDrawn()) {
      return;
    }
    if (bubble_widget_) {
      bubble_widget_->Activate();
      return;
    }
    const bool target = !approved();
    auto body =
        target ? Text(
                     u"Lesezeichen und Ordner dieses Profils werden mit deinen "
                     u"Ahoi-Geräten "
                     u"zusammengeführt. Dafür müssen Ahoi-Sync, Konto und "
                     u"Schlüssel bereit sein. "
                     u"Diese Freigabe schaltet Ahoi-Sync nicht ein. Ohne "
                     u"Freigabe bleiben "
                     u"lokale Lesezeichen vollständig nutzbar.",
                     u"Bookmarks and folders in this profile will merge with "
                     u"your Ahoi devices. "
                     u"Ahoi Sync, your account and keys must also be ready. "
                     u"This approval does "
                     u"not enable Ahoi Sync. Local bookmarks remain fully "
                     u"usable without it.")
               : Text(
                     u"Neue Lesezeichen-Übertragungen auf diesem Gerät werden "
                     u"angehalten. "
                     u"Lokale Lesezeichen und bereits übertragene Daten werden "
                     u"nicht gelöscht.",
                     u"New bookmark transfers on this device will stop. Local "
                     u"bookmarks "
                     u"and previously transferred data will not be deleted.");
    if (const auto issue = IssueExplanation(); !issue.empty()) {
      body = issue + u"\n\n" + body;
    }
    auto dialog =
        ui::DialogModel::Builder()
            .SetTitle(Text(u"Lesezeichen synchronisieren", u"Sync bookmarks"))
            .AddParagraph(ui::DialogModelLabel(body))
            .AddOkButton(base::BindOnce(&BookmarkSyncControl::Approve,
                                        weak_ptr_factory_.GetWeakPtr(), target),
                         ui::DialogModel::Button::Params().SetLabel(
                             target ? Text(u"Lesezeichen freigeben",
                                           u"Approve bookmark sync")
                                    : Text(u"Lesezeichen-Sync stoppen",
                                           u"Stop bookmark sync")))
            .AddCancelButton(base::DoNothing(),
                             ui::DialogModel::Button::Params().SetLabel(
                                 Text(u"Abbrechen", u"Cancel")))
            .Build();
    auto delegate = std::make_unique<views::BubbleDialogModelHost>(
        std::move(dialog), button_, views::BubbleBorder::TOP_RIGHT);
    bubble_widget_ = views::BubbleDialogDelegate::CreateBubble(
        delegate.get(), base::BindOnce(&BookmarkSyncControl::OnClosed,
                                       weak_ptr_factory_.GetWeakPtr()));
    // BubbleDialogModelHost sets SetOwnedByWidget() in its constructor. The
    // client owns the Widget, but must not own/delete its delegate a second
    // time.
    delegate.release();
    bubble_widget_->Show();
  }
  void Approve(bool enabled) {
    if (auto* service =
            sync::ProfileSyncServiceFactory::GetForProfile(profile_)) {
      service->SetBookmarkSyncEnabled(enabled);
    }
  }
  void OnClosed(views::Widget::ClosedReason) { bubble_widget_.reset(); }

  const raw_ptr<Profile> profile_;
  raw_ptr<SidebarBookmarkButton> button_ = nullptr;
  PrefChangeRegistrar pref_observer_;
  base::CallbackListSubscription status_subscription_;
  std::unique_ptr<views::Widget> bubble_widget_;
  base::WeakPtrFactory<BookmarkSyncControl> weak_ptr_factory_{this};
};

BEGIN_METADATA(BookmarkSyncControl)
END_METADATA

}  // namespace

std::unique_ptr<views::View> CreateBookmarkSyncControl(Profile* profile) {
  if (!profile || profile->IsOffTheRecord() || !profile->IsRegularProfile()) {
    return nullptr;
  }
  return std::make_unique<BookmarkSyncControl>(profile);
}

}  // namespace ahoi::sidebar
