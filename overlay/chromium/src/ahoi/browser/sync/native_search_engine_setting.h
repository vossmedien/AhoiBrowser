// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_NATIVE_SEARCH_ENGINE_SETTING_H_
#define AHOI_BROWSER_SYNC_NATIVE_SEARCH_ENGINE_SETTING_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/search_engines/template_url_service_observer.h"

class Profile;
class TemplateURLService;

namespace ahoi::sync {

// UI-sequence adapter for the native default-search choice. It neither owns a
// keywords database nor starts/takes over a Google Sync processor. Model/pref
// notifications are coalesced onto the owning sequence; `changed` may delete
// us.
class NativeSearchEngineSetting final : public TemplateURLServiceObserver,
                                        public ProfileObserver {
 public:
  NativeSearchEngineSetting(Profile* profile, base::RepeatingClosure changed);
  NativeSearchEngineSetting(const NativeSearchEngineSetting&) = delete;
  NativeSearchEngineSetting& operator=(const NativeSearchEngineSetting&) =
      delete;
  ~NativeSearchEngineSetting() override;

  bool ready() const;
  // Only a supported explicit USER choice is exported. The caller must opt into
  // null for a real reset; a fresh native fallback is not a local mutation.
  std::optional<std::string> Read(bool include_default_reset = false) const;
  // Strings select an existing, unmodified built-in engine. Missing/custom or
  // policy-controlled entries fail closed; no peer URL or keyword is accepted.
  bool Apply(std::string_view value_json);

 private:
  void OnTemplateURLServiceChanged() override;
  void OnTemplateURLServiceShuttingDown() override;
  void OnProfileWillBeDestroyed(Profile* profile) override;
  void ScheduleChanged();
  void DispatchChanged();
  void Disconnect();
  bool UserChoiceAllowed() const;

  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<TemplateURLService> service_ = nullptr;
  base::RepeatingClosure changed_;
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  base::ScopedObservation<TemplateURLService, TemplateURLServiceObserver>
      service_observation_{this};
  PrefChangeRegistrar pref_registrar_;
  bool notification_pending_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<NativeSearchEngineSetting> weak_factory_{this};
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_NATIVE_SEARCH_ENGINE_SETTING_H_
