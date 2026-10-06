// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/toast/ahoi_toast.h"

#include "base/i18n/rtl.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/vector_icons/vector_icons.h"

namespace ahoi::toast {

namespace {

bool German() {
  return base::i18n::GetConfiguredLocale().starts_with("de");
}

std::u16string Text(const char* de, const char* en) {
  return base::UTF8ToUTF16(German() ? de : en);
}

// "Tab" / "3 Tabs", English "Tab" / "3 tabs" (a sentence start).
std::u16string Tabs(size_t count) {
  return count == 1 ? u"Tab"
                    : base::NumberToString16(count) +
                          (German() ? u" Tabs" : u" tabs");
}

}  // namespace

std::u16string Message(Event event,
                       const std::u16string& detail,
                       size_t count) {
  switch (event) {
    case Event::kBackgroundTabOpened:
      return Text("Im Hintergrund geöffnet", "Opened in the background");
    case Event::kLinkCopied:
      return Text("Link kopiert", "Link copied");
    case Event::kTabSaved:
      return Text("Tab gespeichert", "Tab saved");
    case Event::kArchived:
      return Tabs(count) + Text(" archiviert", " archived");
    case Event::kMovedToWorkspace:
      return detail.empty()
                 ? Tabs(count) + Text(" verschoben", " moved")
                 : Tabs(count) + Text(" nach „", " moved to “") + detail +
                       Text("“ verschoben", "”");
    case Event::kClosed:
      return Tabs(count) + Text(" geschlossen", " closed");
    case Event::kDownloadStarted:
      return detail.empty()
                 ? Text("Download gestartet", "Download started")
                 : Text("Lädt herunter: ", "Downloading: ") + detail;
  }
}

const gfx::VectorIcon& Icon(Event event) {
  switch (event) {
    case Event::kBackgroundTabOpened:
      return vector_icons::kArrowRightAltIcon;
    case Event::kLinkCopied:
      return vector_icons::kLinkIcon;
    case Event::kTabSaved:
      return vector_icons::kStarFilledIcon;
    case Event::kArchived:
      return vector_icons::kHistoryIcon;
    case Event::kMovedToWorkspace:
      return vector_icons::kArrowForwardIcon;
    case Event::kClosed:
      return vector_icons::kCloseIcon;
    case Event::kDownloadStarted:
      return vector_icons::kDownloadIcon;
  }
}

}  // namespace ahoi::toast
