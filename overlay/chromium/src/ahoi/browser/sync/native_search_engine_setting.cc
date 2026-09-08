// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/native_search_engine_setting.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/default_search_manager.h"
#include "components/search_engines/search_engines_pref_names.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_data_util.h"
#include "components/search_engines/template_url_prepopulate_data.h"
#include "components/search_engines/template_url_service.h"

namespace ahoi::sync {
namespace {

struct Choice {
  std::string_view name;
  int prepopulate_id;
};

// Native IDs from third_party/search_engines_data/resources/definitions/
// prepopulated_engines.json; names match MobileSearchEngine's portable choices.
constexpr Choice kChoices[] = {{"duckDuckGo", 92}, {"google", 1}, {"bing", 3}};

std::unique_ptr<TemplateURLData> BuiltinData(int prepopulate_id) {
  // Use code-owned definitions, never profile SearchProviderOverrides or URLs
  // supplied by a peer. Only the choice identifier crosses the sync boundary.
  const auto* engine =
      TemplateURLPrepopulateData::GetPrepopulatedEngineFromBuiltInData(
          prepopulate_id, {});
  return engine ? TemplateURLDataFromPrepopulatedEngine(*engine) : nullptr;
}

bool MatchesDefinition(const TemplateURL* engine,
                       const TemplateURLData& definition,
                       const TemplateURLService& service) {
  if (!engine || engine->type() != TemplateURL::NORMAL ||
      engine->data().CreatedByPolicy() || engine->data().starter_pack_id != 0 ||
      engine->data().prepopulate_id != definition.prepopulate_id ||
      !TemplateURL::MatchesData(engine, &definition,
                                service.search_terms_data())) {
    return false;
  }
  // Retain only native runtime identity/activity differences. Full data
  // equality also checks endpoint/POST/header-related fields beyond
  // MatchesData's subset. Favicon changes are native browsing metadata, not a
  // different search choice.
  TemplateURLData comparable(engine->data());
  comparable.id = definition.id;
  comparable.date_created = definition.date_created;
  comparable.last_modified = definition.last_modified;
  comparable.last_visited = definition.last_visited;
  comparable.usage_count = definition.usage_count;
  comparable.sync_guid = definition.sync_guid;
  comparable.is_active = definition.is_active;
  comparable.favicon_url = definition.favicon_url;
  // MatchesData already verified native Google keyword equivalence.
  comparable.SetKeyword(definition.keyword());
  return comparable == definition;
}

TemplateURL* FindMatchingEngine(TemplateURLService& service,
                                const TemplateURLData& definition) {
  TemplateURL* result = nullptr;
  for (TemplateURL* engine : service.GetTemplateURLs()) {
    if (engine->data().prepopulate_id != definition.prepopulate_id) {
      continue;
    }
    // Ambiguity or a user-edited built-in is not permission to replace it.
    if (result || !MatchesDefinition(engine, definition, service)) {
      return nullptr;
    }
    result = engine;
  }
  return result;
}

}  // namespace

NativeSearchEngineSetting::NativeSearchEngineSetting(
    Profile* profile,
    base::RepeatingClosure changed)
    : profile_(profile), changed_(std::move(changed)) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // The native factory redirects OTR requests to the original profile. Reject
  // them before asking the factory, so this adapter cannot cross that boundary.
  if (!profile_ || profile_->IsOffTheRecord() ||
      !profile_->IsRegularProfile()) {
    profile_ = nullptr;
    return;
  }
  profile_observation_.Observe(profile_);
  service_ = TemplateURLServiceFactory::GetForProfile(profile_);
  if (!service_) {
    return;
  }
  service_observation_.Observe(service_);
  if (profile_->GetPrefs()->FindPreference(
          DefaultSearchManager::kDefaultSearchProviderDataPrefName)) {
    pref_registrar_.Init(profile_->GetPrefs());
    // A USER->FALLBACK change can keep the same effective engine pointer.
    // Observe its native selection record as well, but never export that dict.
    pref_registrar_.Add(
        DefaultSearchManager::kDefaultSearchProviderDataPrefName,
        base::BindRepeating(&NativeSearchEngineSetting::ScheduleChanged,
                            weak_factory_.GetWeakPtr()));
  }
  ScheduleChanged();
  service_->Load();
}

NativeSearchEngineSetting::~NativeSearchEngineSetting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  Disconnect();
}

bool NativeSearchEngineSetting::ready() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return profile_ && service_ && service_->loaded() &&
         !profile_->IsOffTheRecord() && profile_->IsRegularProfile() &&
         profile_->GetPrefs()->FindPreference(
             DefaultSearchManager::kDefaultSearchProviderDataPrefName);
}

bool NativeSearchEngineSetting::UserChoiceAllowed() const {
  if (!ready()) {
    return false;
  }
  // DefaultSearchManager can reconcile even an otherwise built-in selection
  // through profile-specific overrides. Never select an unreviewed definition
  // indirectly through that path, or turn it into one of our three choices.
  const auto* overrides =
      profile_->GetPrefs()->FindPreference(prefs::kSearchProviderOverrides);
  if (overrides && (!overrides->GetValue()->is_list() ||
                    !overrides->GetValue()->GetList().empty())) {
    return false;
  }
  const auto source = service_->default_search_provider_source();
  return (source == DefaultSearchManager::FROM_USER ||
          source == DefaultSearchManager::FROM_FALLBACK) &&
         source == service_->GetDefaultSearchManager()
                       ->GetDefaultSearchEngineSource() &&
         profile_->GetPrefs()->IsUserModifiablePreference(
             DefaultSearchManager::kDefaultSearchProviderDataPrefName);
}

std::optional<std::string> NativeSearchEngineSetting::Read(
    bool include_default_reset) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!UserChoiceAllowed()) {
    return std::nullopt;
  }
  const base::Value* user = profile_->GetPrefs()->GetUserPrefValue(
      DefaultSearchManager::kDefaultSearchProviderDataPrefName);
  if (service_->default_search_provider_source() ==
      DefaultSearchManager::FROM_FALLBACK) {
    // An unrecognized/corrupt user dictionary is not an absent user choice.
    return !user && include_default_reset
               ? std::make_optional(std::string("null"))
               : std::nullopt;
  }
  if (!user || !user->is_dict() || user->GetDict().empty()) {
    return std::nullopt;
  }
  const TemplateURL* selected = service_->GetDefaultSearchProvider();
  const auto* managed_value =
      service_->GetDefaultSearchManager()->GetDefaultSearchEngine(nullptr);
  if (!managed_value || !selected ||
      !TemplateURL::MatchesData(selected, managed_value,
                                service_->search_terms_data())) {
    return std::nullopt;  // Includes the native non-persisting load-failure
                          // path.
  }
  for (const auto& choice : kChoices) {
    if (selected->data().prepopulate_id != choice.prepopulate_id) {
      continue;
    }
    auto builtin = BuiltinData(choice.prepopulate_id);
    if (builtin && MatchesDefinition(selected, *builtin, *service_)) {
      return base::WriteJson(base::Value(std::string(choice.name)));
    }
  }
  return std::nullopt;
}

bool NativeSearchEngineSetting::Apply(std::string_view value_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto value = base::JSONReader::Read(value_json, base::JSON_PARSE_RFC);
  if (!value || !UserChoiceAllowed() || profile_->GetPrefs()->ReadOnly()) {
    return false;
  }
  if (value->is_none()) {
    if (Read(true) == std::make_optional(std::string("null"))) {
      return true;
    }
    const auto* fallback =
        service_->GetDefaultSearchManager()->GetFallbackSearchEngine();
    // Native fallback application can repair/overwrite a prepopulated entry.
    // Do not invoke that repair when the corresponding row is missing/custom.
    if (!fallback || !FindMatchingEngine(*service_, *fallback)) {
      return false;
    }
    const auto self = weak_factory_.GetWeakPtr();
    service_->SetUserSelectedDefaultSearchProvider(nullptr);
    return self && self->Read(true) == std::make_optional(std::string("null"));
  }
  if (!value->is_string()) {
    return false;
  }
  for (const auto& choice : kChoices) {
    if (value->GetString() != choice.name) {
      continue;
    }
    auto builtin = BuiltinData(choice.prepopulate_id);
    TemplateURL* selected =
        builtin ? FindMatchingEngine(*service_, *builtin) : nullptr;
    if (!selected || (selected != service_->GetDefaultSearchProvider() &&
                      !service_->CanMakeDefault(selected))) {
      return false;
    }
    const auto self = weak_factory_.GetWeakPtr();
    service_->SetUserSelectedDefaultSearchProvider(selected);
    return self && self->Read() ==
                       base::WriteJson(base::Value(std::string(choice.name)));
  }
  return false;
}

void NativeSearchEngineSetting::OnTemplateURLServiceChanged() {
  ScheduleChanged();
}

void NativeSearchEngineSetting::OnTemplateURLServiceShuttingDown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  pref_registrar_.Reset();
  service_observation_.Reset();
  service_ = nullptr;
  ScheduleChanged();
}

void NativeSearchEngineSetting::OnProfileWillBeDestroyed(Profile* profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (profile == profile_) {
    Disconnect();
    ScheduleChanged();
  }
}

void NativeSearchEngineSetting::Disconnect() {
  pref_registrar_.Reset();
  service_observation_.Reset();
  profile_observation_.Reset();
  service_ = nullptr;
  profile_ = nullptr;
}

void NativeSearchEngineSetting::ScheduleChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (notification_pending_) {
    return;
  }
  notification_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&NativeSearchEngineSetting::DispatchChanged,
                                weak_factory_.GetWeakPtr()));
}

void NativeSearchEngineSetting::DispatchChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  notification_pending_ = false;
  auto changed = changed_;
  if (changed) {
    changed.Run();  // No access to this after the external callback.
  }
}

}  // namespace ahoi::sync
