// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/updater/update_strings.h"

#include <array>
#include <cstddef>

namespace ahoi::updater {
namespace {

using Table = std::array<std::string_view,
                         static_cast<std::size_t>(UpdateString::kCount)>;

constexpr Table kEnglish = {
    "Check for Updates...",
    "Software Updates...",
    "Software Updates",
    "Status",
    "Channel",
    "Check automatically",
    "Download updates automatically",
    "Check Now",
    "Done",
    "Updates are unavailable because the secure release configuration is "
    "incomplete.",
    "Ready to check for updates.",
    "Checking for updates...",
    "An update is available.",
    "Downloading the update...",
    "The update is ready to install.",
    "Installing the update...",
    "AhoiBrowser is relaunching...",
    "AhoiBrowser is up to date.",
    "The update could not be completed.",
    "Updates require an HTTPS feed, a pinned Ed25519 public key, signed feeds, "
    "and verification before extraction.",
};

constexpr Table kBritishEnglish = {
    "Check for Updates...",
    "Software Updates...",
    "Software Updates",
    "Status",
    "Channel",
    "Check automatically",
    "Download updates automatically",
    "Check Now",
    "Done",
    "Updates are unavailable because the secure release configuration is "
    "incomplete.",
    "Ready to check for updates.",
    "Checking for updates...",
    "An update is available.",
    "Downloading the update...",
    "The update is ready to install.",
    "Installing the update...",
    "AhoiBrowser is relaunching...",
    "AhoiBrowser is up to date.",
    "The update could not be completed.",
    "Updates require an HTTPS feed, a pinned Ed25519 public key, signed feeds, "
    "and verification before extraction.",
};

constexpr Table kGerman = {
    "Nach Updates suchen...",
    "Softwareupdates...",
    "Softwareupdates",
    "Status",
    "Kanal",
    "Automatisch suchen",
    "Updates automatisch laden",
    "Jetzt suchen",
    "Fertig",
    "Updates sind nicht verfügbar, weil die sichere Release-Konfiguration "
    "unvollständig ist.",
    "Bereit für die Updatesuche.",
    "Updates werden gesucht...",
    "Ein Update ist verfügbar.",
    "Das Update wird geladen...",
    "Das Update kann installiert werden.",
    "Das Update wird installiert...",
    "AhoiBrowser wird neu gestartet...",
    "AhoiBrowser ist aktuell.",
    "Das Update konnte nicht abgeschlossen werden.",
    "Updates erfordern einen HTTPS-Feed, einen gepinnten öffentlichen "
    "Ed25519-Schlüssel, signierte Feeds und eine Prüfung vor dem Entpacken.",
};

const Table& TableForLocale(std::string_view locale) {
  if (locale == "de" || locale.starts_with("de-") ||
      locale.starts_with("de_")) {
    return kGerman;
  }
  if (locale == "en-GB" || locale == "en_GB") {
    return kBritishEnglish;
  }
  return kEnglish;
}

}  // namespace

std::string_view LocalizedUpdateString(UpdateString key,
                                       std::string_view locale) {
  const std::size_t index = static_cast<std::size_t>(key);
  if (index >= kEnglish.size()) {
    return kEnglish[static_cast<std::size_t>(UpdateString::kError)];
  }
  return TableForLocale(locale)[index];
}

}  // namespace ahoi::updater
