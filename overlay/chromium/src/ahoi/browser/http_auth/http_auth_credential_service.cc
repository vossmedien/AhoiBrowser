// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_credential_service.h"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <utility>

#include "ahoi/browser/http_auth/http_auth_credential_service_internal.h"
#include "ahoi/browser/http_auth/http_auth_prefs.h"
#include "ahoi/browser/http_auth/http_auth_secret_util.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_store/password_form_converters.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/prefs/pref_service.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "net/base/host_port_pair.h"
#include "net/http/http_auth_scheme.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace ahoi {

HttpAuthCredential::HttpAuthCredential() = default;

HttpAuthCredential::HttpAuthCredential(HttpAuthCredential&& other) noexcept
    : metadata(std::move(other.metadata)), password(std::move(other.password)) {
  SecurelyClearHttpAuthSecret(&other.password);
}

HttpAuthCredential& HttpAuthCredential::operator=(
    HttpAuthCredential&& other) noexcept {
  if (this != &other) {
    SecurelyClearHttpAuthSecret(&password);
    metadata = std::move(other.metadata);
    password = std::move(other.password);
    SecurelyClearHttpAuthSecret(&other.password);
  }
  return *this;
}

HttpAuthCredential::~HttpAuthCredential() {
  SecurelyClearHttpAuthSecret(&password);
}

class HttpAuthCredentialService::CredentialQuery final
    : public password_manager::PasswordStoreConsumer {
 public:
  using Completion =
      base::OnceCallback<void(CredentialQuery*,
                              password_manager::LoginsResultOrError)>;

  explicit CredentialQuery(Completion completion)
      : completion_(std::move(completion)) {}

  CredentialQuery(const CredentialQuery&) = delete;
  CredentialQuery& operator=(const CredentialQuery&) = delete;

  base::WeakPtr<CredentialQuery> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  void OnGetPasswordStoreResultsOrErrorFrom(
      password_manager::PasswordStoreInterface* store,
      password_manager::LoginsResultOrError results_or_error) override {
    std::move(completion_).Run(this, std::move(results_or_error));
  }

 private:
  Completion completion_;
  base::WeakPtrFactory<CredentialQuery> weak_factory_{this};
};

// static
void HttpAuthCredentialService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  http_auth_prefs::RegisterProfilePrefs(registry);
}

HttpAuthCredentialService::HttpAuthCredentialService(
    PrefService* prefs,
    scoped_refptr<password_manager::PasswordStoreInterface> password_store,
    bool profile_is_incognito_for_testing)
    : prefs_(prefs),
      password_store_(std::move(password_store)),
      profile_is_incognito_for_testing_(profile_is_incognito_for_testing) {}

HttpAuthCredentialService::~HttpAuthCredentialService() = default;

void HttpAuthCredentialService::GetCredentials(
    const HttpAuthProtectionSpace& protection_space,
    std::string_view request_path,
    HttpAuthRequestContext request_context,
    HttpAuthSelectionMode selection_mode,
    CredentialsCallback callback) {
  if (!callback) {
    return;
  }
  if (!protection_space.IsValid() || !password_store_ ||
      (request_context == HttpAuthRequestContext::kIncognito &&
       selection_mode == HttpAuthSelectionMode::kAutomatic) ||
      (profile_is_incognito_for_testing_ &&
       request_context != HttpAuthRequestContext::kIncognito)) {
    std::move(callback).Run({});
    return;
  }

  const http_auth_internal::MetadataState metadata_state =
      http_auth_internal::ReadMetadataState(prefs_);
  if (!metadata_state.valid) {
    std::move(callback).Run({});
    return;
  }
  if (selection_mode == HttpAuthSelectionMode::kAutomatic && !prefs_) {
    std::move(callback).Run({});
    return;
  }

  password_manager::PasswordForm form =
      MakePasswordForm(protection_space, net::AuthCredentials(), base::Time());
  auto query = std::make_unique<CredentialQuery>(base::BindOnce(
      &HttpAuthCredentialService::OnCredentialLookupComplete,
      weak_factory_.GetWeakPtr(), HttpAuthProtectionSpace(protection_space),
      std::string(request_path), std::move(request_context),
      std::move(selection_mode), std::move(callback)));
  CredentialQuery* query_ptr = query.get();
  pending_queries_.push_back(PendingQuery{std::move(query)});
  password_store_->GetLogins(password_manager::PasswordFormDigest(form),
                             query_ptr->GetWeakPtr());
}

void HttpAuthCredentialService::RecordSuccessfulAuthentication(
    const HttpAuthProtectionSpace& protection_space,
    std::string_view request_path,
    const net::AuthCredentials& credentials,
    HttpAuthRequestContext request_context,
    bool user_confirmed_insecure_http,
    base::OnceClosure done) {
  if (!protection_space.IsValid() || !password_store_ ||
      request_context == HttpAuthRequestContext::kIncognito ||
      profile_is_incognito_for_testing_ || credentials.username().empty() ||
      credentials.password().empty() ||
      (protection_space.origin.scheme() == url::kHttpScheme &&
       !user_confirmed_insecure_http) ||
      IsNeverSaveForRealm(protection_space)) {
    std::move(done).Run();
    return;
  }

  password_manager::PasswordForm form =
      MakePasswordForm(protection_space, credentials, base::Time::Now());
  auto query = std::make_unique<CredentialQuery>(base::BindOnce(
      &HttpAuthCredentialService::OnSaveLookupComplete,
      weak_factory_.GetWeakPtr(), HttpAuthProtectionSpace(protection_space),
      std::string(request_path), net::AuthCredentials(credentials),
      std::move(request_context), user_confirmed_insecure_http,
      std::move(done)));
  CredentialQuery* query_ptr = query.get();
  pending_queries_.push_back(PendingQuery{std::move(query)});
  password_store_->GetLogins(password_manager::PasswordFormDigest(form),
                             query_ptr->GetWeakPtr());
}

void HttpAuthCredentialService::RecordAuthenticationFailure(
    const HttpAuthProtectionSpace& protection_space,
    std::string_view request_path,
    std::u16string_view username,
    HttpAuthRequestContext request_context) {
  // Keep this method intentionally side-effect free. Named parameters make it
  // harder for a future integration to accidentally delete on a single 401.
  (void)protection_space;
  (void)request_path;
  (void)username;
  (void)request_context;
}

void HttpAuthCredentialService::DeleteCredential(
    const HttpAuthProtectionSpace& protection_space,
    std::u16string_view username,
    HttpAuthRequestContext request_context,
    base::OnceClosure done) {
  if (!protection_space.IsValid() || !password_store_ || username.empty() ||
      request_context == HttpAuthRequestContext::kIncognito ||
      profile_is_incognito_for_testing_) {
    std::move(done).Run();
    return;
  }
  password_manager::PasswordForm form =
      MakePasswordForm(protection_space, net::AuthCredentials(), base::Time());
  auto query = std::make_unique<CredentialQuery>(base::BindOnce(
      &HttpAuthCredentialService::OnDeleteLookupComplete,
      weak_factory_.GetWeakPtr(), HttpAuthProtectionSpace(protection_space),
      std::optional<std::u16string>(username), std::move(request_context),
      std::move(done)));
  CredentialQuery* query_ptr = query.get();
  pending_queries_.push_back(PendingQuery{std::move(query)});
  password_store_->GetLogins(password_manager::PasswordFormDigest(form),
                             query_ptr->GetWeakPtr());
}

void HttpAuthCredentialService::UpdateCredential(
    const HttpAuthProtectionSpace& protection_space,
    std::u16string_view old_username,
    std::u16string new_username,
    std::u16string new_password,
    HttpAuthRequestContext request_context,
    UpdateCallback done) {
  if (!done) {
    SecurelyClearHttpAuthSecret(&new_password);
    return;
  }
  ScopedHttpAuthSecret scoped_password(std::move(new_password));
  if (!protection_space.IsValid() || !password_store_ || old_username.empty() ||
      new_username.empty() || scoped_password.empty() ||
      request_context == HttpAuthRequestContext::kIncognito ||
      profile_is_incognito_for_testing_ ||
      !CanRenameCredentialMetadata(protection_space, old_username,
                                   new_username)) {
    scoped_password.Clear();
    std::move(done).Run(false);
    return;
  }

  password_manager::PasswordForm form =
      MakePasswordForm(protection_space, net::AuthCredentials(), base::Time());
  auto query = std::make_unique<CredentialQuery>(base::BindOnce(
      &HttpAuthCredentialService::OnUpdateLookupComplete,
      weak_factory_.GetWeakPtr(), HttpAuthProtectionSpace(protection_space),
      std::u16string(old_username), std::move(new_username),
      std::move(scoped_password), request_context, std::move(done)));
  CredentialQuery* query_ptr = query.get();
  pending_queries_.push_back(PendingQuery{std::move(query)});
  password_store_->GetLogins(password_manager::PasswordFormDigest(form),
                             query_ptr->GetWeakPtr());
}

void HttpAuthCredentialService::DeleteRealm(
    const HttpAuthProtectionSpace& protection_space,
    HttpAuthRequestContext request_context,
    base::OnceClosure done) {
  if (!protection_space.IsValid() || !password_store_ ||
      request_context == HttpAuthRequestContext::kIncognito ||
      profile_is_incognito_for_testing_) {
    std::move(done).Run();
    return;
  }
  password_manager::PasswordForm form =
      MakePasswordForm(protection_space, net::AuthCredentials(), base::Time());
  auto query = std::make_unique<CredentialQuery>(base::BindOnce(
      &HttpAuthCredentialService::OnDeleteLookupComplete,
      weak_factory_.GetWeakPtr(), HttpAuthProtectionSpace(protection_space),
      std::nullopt, std::move(request_context), std::move(done)));
  CredentialQuery* query_ptr = query.get();
  pending_queries_.push_back(PendingQuery{std::move(query)});
  password_store_->GetLogins(password_manager::PasswordFormDigest(form),
                             query_ptr->GetWeakPtr());
}

// static
network::mojom::ClearDataFilterPtr
HttpAuthCredentialService::BuildHttpAuthCacheFilter(
    const HttpAuthProtectionSpace& protection_space) {
  if (!protection_space.IsValid()) {
    return nullptr;
  }
  return BuildHttpAuthCacheFilterForOrigin(protection_space.OriginUrl());
}

// static
network::mojom::ClearDataFilterPtr
HttpAuthCredentialService::BuildHttpAuthCacheFilterForOrigin(
    const GURL& origin_url) {
  if (!origin_url.SchemeIsHTTPOrHTTPS()) {
    return nullptr;
  }
  const url::Origin origin = url::Origin::Create(origin_url);
  if (origin.opaque()) {
    return nullptr;
  }
  network::mojom::ClearDataFilterPtr filter =
      network::mojom::ClearDataFilter::New();
  filter->type = network::mojom::ClearDataFilter::Type::DELETE_MATCHES;
  filter->origins.push_back(origin);
  return filter;
}

void HttpAuthCredentialService::OnCredentialLookupComplete(
    HttpAuthProtectionSpace protection_space,
    std::string request_path,
    HttpAuthRequestContext request_context,
    HttpAuthSelectionMode selection_mode,
    CredentialsCallback callback,
    CredentialQuery* query,
    password_manager::LoginsResultOrError results_or_error) {
  RemovePendingQuery(query);
  if (!std::holds_alternative<password_manager::LoginsResult>(
          results_or_error)) {
    if (callback) {
      std::move(callback).Run({});
    }
    return;
  }
  password_manager::LoginsResult results =
      std::move(std::get<password_manager::LoginsResult>(results_or_error));
  if (!callback) {
    for (password_manager::StoredCredential& stored : results) {
      SecurelyClearHttpAuthSecret(&stored.password_value);
    }
    return;
  }

  const http_auth_internal::MetadataState metadata_state =
      http_auth_internal::ReadMetadataState(prefs_);
  if (!metadata_state.valid) {
    for (password_manager::StoredCredential& stored : results) {
      SecurelyClearHttpAuthSecret(&stored.password_value);
    }
    std::move(callback).Run({});
    return;
  }

  const bool explicit_selection =
      selection_mode == HttpAuthSelectionMode::kExplicitUserSelection;
  std::vector<HttpAuthCredential> credentials;
  (void)request_context;
  for (password_manager::StoredCredential& stored : results) {
    if (!MatchesStoredCredential(protection_space, stored)) {
      continue;
    }
    std::optional<HttpAuthCredentialMetadata> metadata;
    for (const HttpAuthCredentialMetadata& candidate :
         metadata_state.credentials) {
      if (candidate.username == stored.username_value &&
          http_auth_internal::IsSameRealmAndNetwork(candidate.protection_space,
                                                    protection_space)) {
        metadata = candidate;
        break;
      }
    }

    if (!explicit_selection &&
        (!metadata ||
         !metadata->protection_space.Matches(protection_space, request_path))) {
      continue;
    }

    HttpAuthCredential result;
    if (metadata) {
      result.metadata = std::move(*metadata);
    } else {
      result.metadata.protection_space = protection_space;
      result.metadata.protection_space.AddPermittedPath(request_path);
      result.metadata.username = stored.username_value;
      result.metadata.last_successful = stored.date_last_used;
    }
    result.password = std::move(stored.password_value);
    credentials.push_back(std::move(result));
  }
  for (password_manager::StoredCredential& stored : results) {
    SecurelyClearHttpAuthSecret(&stored.password_value);
  }

  std::sort(credentials.begin(), credentials.end(),
            &http_auth_internal::IsCredentialNewer);
  std::move(callback).Run(std::move(credentials));
}

void HttpAuthCredentialService::OnSaveLookupComplete(
    HttpAuthProtectionSpace protection_space,
    std::string request_path,
    net::AuthCredentials credentials,
    HttpAuthRequestContext request_context,
    bool user_confirmed_insecure_http,
    base::OnceClosure done,
    CredentialQuery* query,
    password_manager::LoginsResultOrError results_or_error) {
  RemovePendingQuery(query);
  if (!std::holds_alternative<password_manager::LoginsResult>(
          results_or_error) ||
      request_context == HttpAuthRequestContext::kIncognito ||
      profile_is_incognito_for_testing_ ||
      (protection_space.origin.scheme() == url::kHttpScheme &&
       !user_confirmed_insecure_http) ||
      IsNeverSaveForRealm(protection_space)) {
    std::move(done).Run();
    return;
  }

  const base::Time now = base::Time::Now();
  password_manager::PasswordForm form =
      MakePasswordForm(protection_space, credentials, now);
  password_manager::LoginsResult results =
      std::move(std::get<password_manager::LoginsResult>(results_or_error));
  password_manager::StoredCredential* existing = nullptr;
  for (password_manager::StoredCredential& stored : results) {
    if (MatchesStoredCredential(protection_space, stored) &&
        stored.username_value == credentials.username()) {
      existing = &stored;
      break;
    }
  }

  const std::u16string username = credentials.username();
  auto write_done = base::BindOnce(
      [](base::WeakPtr<HttpAuthCredentialService> service,
         HttpAuthProtectionSpace protection_space, std::string request_path,
         std::u16string username, base::Time now, base::OnceClosure done) {
        if (service) {
          service->RecordMetadataAfterSuccess(protection_space, request_path,
                                              username, now);
        }
        std::move(done).Run();
      },
      weak_factory_.GetWeakPtr(), protection_space, std::move(request_path),
      username, now, std::move(done));

  if (existing) {
    const bool password_changed =
        existing->password_value != credentials.password();
    existing->password_value = credentials.password();
    existing->date_last_used = now;
    if (password_changed) {
      existing->date_password_modified = now;
    }
    password_store_->UpdateLogin(std::move(*existing), std::move(write_done));
    return;
  }

  password_store_->AddLogin(password_manager::FromPasswordForm(std::move(form)),
                            std::move(write_done));
}

void HttpAuthCredentialService::OnDeleteLookupComplete(
    HttpAuthProtectionSpace protection_space,
    std::optional<std::u16string> username,
    HttpAuthRequestContext request_context,
    base::OnceClosure done,
    CredentialQuery* query,
    password_manager::LoginsResultOrError results_or_error) {
  RemovePendingQuery(query);
  if (!std::holds_alternative<password_manager::LoginsResult>(
          results_or_error) ||
      request_context == HttpAuthRequestContext::kIncognito ||
      profile_is_incognito_for_testing_) {
    std::move(done).Run();
    return;
  }

  password_manager::LoginsResult results =
      std::move(std::get<password_manager::LoginsResult>(results_or_error));
  for (const password_manager::StoredCredential& stored : results) {
    if (!MatchesStoredCredential(protection_space, stored) ||
        (username && stored.username_value != *username)) {
      continue;
    }
    password_store_->RemoveLogin(FROM_HERE, stored);
  }
  for (password_manager::StoredCredential& stored : results) {
    SecurelyClearHttpAuthSecret(&stored.password_value);
  }
  DeleteMetadata(
      protection_space,
      username ? std::optional<std::u16string_view>(*username) : std::nullopt);
  std::move(done).Run();
}

void HttpAuthCredentialService::OnUpdateLookupComplete(
    HttpAuthProtectionSpace protection_space,
    std::u16string old_username,
    std::u16string new_username,
    ScopedHttpAuthSecret new_password,
    HttpAuthRequestContext request_context,
    UpdateCallback done,
    CredentialQuery* query,
    password_manager::LoginsResultOrError results_or_error) {
  RemovePendingQuery(query);
  if (!std::holds_alternative<password_manager::LoginsResult>(
          results_or_error)) {
    new_password.Clear();
    std::move(done).Run(false);
    return;
  }
  password_manager::LoginsResult results =
      std::move(std::get<password_manager::LoginsResult>(results_or_error));
  if (request_context == HttpAuthRequestContext::kIncognito ||
      profile_is_incognito_for_testing_ || new_password.empty() ||
      !CanRenameCredentialMetadata(protection_space, old_username,
                                   new_username)) {
    for (password_manager::StoredCredential& stored : results) {
      SecurelyClearHttpAuthSecret(&stored.password_value);
    }
    new_password.Clear();
    std::move(done).Run(false);
    return;
  }

  password_manager::StoredCredential* existing = nullptr;
  bool username_collision = false;
  for (password_manager::StoredCredential& stored : results) {
    if (!MatchesStoredCredential(protection_space, stored)) {
      continue;
    }
    if (stored.username_value == old_username) {
      existing = &stored;
    } else if (stored.username_value == new_username) {
      username_collision = true;
    }
  }
  if (!existing || username_collision) {
    for (password_manager::StoredCredential& stored : results) {
      SecurelyClearHttpAuthSecret(&stored.password_value);
    }
    new_password.Clear();
    std::move(done).Run(false);
    return;
  }

  password_manager::StoredCredential old_primary_key;
  old_primary_key.signon_realm = existing->signon_realm;
  old_primary_key.url = existing->url;
  old_primary_key.username_element = existing->username_element;
  old_primary_key.username_value = existing->username_value;
  old_primary_key.password_element = existing->password_element;
  password_manager::StoredCredential updated =
      password_manager::CloneStoredCredential(*existing);
  const base::Time now = base::Time::Now();
  const bool password_changed = updated.password_value != new_password.value();
  updated.username_value = new_username;
  updated.password_value = new_password.value();
  new_password.Clear();
  if (password_changed) {
    updated.date_password_modified = now;
  }
  for (password_manager::StoredCredential& stored : results) {
    SecurelyClearHttpAuthSecret(&stored.password_value);
  }

  auto write_done = base::BindOnce(
      [](base::WeakPtr<HttpAuthCredentialService> service,
         HttpAuthProtectionSpace protection_space, std::u16string old_username,
         std::u16string new_username, UpdateCallback done) {
        const bool updated =
            service && service->RenameCredentialMetadata(
                           protection_space, old_username, new_username);
        std::move(done).Run(updated);
      },
      weak_factory_.GetWeakPtr(), protection_space, old_username, new_username,
      std::move(done));

  if (old_username == new_username) {
    password_store_->UpdateLogin(std::move(updated), std::move(write_done));
    SecurelyClearHttpAuthSecret(&updated.password_value);
    return;
  }
  password_store_->UpdateLoginWithPrimaryKey(
      std::move(updated), old_primary_key, std::move(write_done));
  SecurelyClearHttpAuthSecret(&updated.password_value);
}

password_manager::PasswordForm HttpAuthCredentialService::MakePasswordForm(
    const HttpAuthProtectionSpace& protection_space,
    const net::AuthCredentials& credentials,
    base::Time now) const {
  password_manager::PasswordForm form;
  form.scheme =
      http_auth_internal::ToPasswordFormScheme(protection_space.scheme);
  form.signon_realm = protection_space.SignonRealm();
  form.url = protection_space.OriginUrl();
  form.username_value = credentials.username();
  form.password_value = credentials.password();
  form.type = password_manager::PasswordForm::Type::kFormSubmission;
  form.date_created = now;
  form.date_last_used = now;
  return form;
}

bool HttpAuthCredentialService::MatchesStoredCredential(
    const HttpAuthProtectionSpace& protection_space,
    const password_manager::StoredCredential& credential) const {
  return credential.scheme == http_auth_internal::ToPasswordFormScheme(
                                  protection_space.scheme) &&
         credential.signon_realm == protection_space.SignonRealm() &&
         url::SchemeHostPort(credential.url) == protection_space.origin;
}

void HttpAuthCredentialService::RemovePendingQuery(CredentialQuery* query) {
  const auto it = std::find_if(pending_queries_.begin(), pending_queries_.end(),
                               [query](const PendingQuery& pending) {
                                 return pending.query.get() == query;
                               });
  if (it != pending_queries_.end()) {
    pending_queries_.erase(it);
  }
}

}  // namespace ahoi
