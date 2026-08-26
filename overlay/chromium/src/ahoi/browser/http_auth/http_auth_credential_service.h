// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_CREDENTIAL_SERVICE_H_
#define AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_CREDENTIAL_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/http_auth/http_auth_secret_util.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "net/base/auth.h"
#include "net/base/network_anonymization_key.h"
#include "net/http/http_auth.h"
#include "services/network/public/mojom/clear_data_filter.mojom.h"
#include "url/scheme_host_port.h"

class GURL;
class PrefService;
class Profile;

namespace password_manager {
class PasswordStoreInterface;
struct StoredCredential;
}  // namespace password_manager

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace ahoi {

// The request context is deliberately supplied by the caller. A service
// instance may be shared by a regular profile and its OTR profile through the
// factory, but an OTR request must never be inferred from the service lifetime.
enum class HttpAuthRequestContext {
  kRegular,
  kIncognito,
};

// Filling a saved credential in an OTR window is only allowed after an
// explicit user selection. This enum prevents an accidental automatic lookup
// from becoming an OTR credential disclosure.
enum class HttpAuthSelectionMode {
  kAutomatic,
  kExplicitUserSelection,
};

// A protection space is the complete key used by Ahoi's HTTP-auth layer. It
// intentionally contains more than a host name: target, scheme, effective
// port, realm, path protection and the Network Anonymization Key all take part
// in matching. Passwords are never members of this type.
struct HttpAuthProtectionSpace {
  HttpAuthProtectionSpace();
  HttpAuthProtectionSpace(
      net::HttpAuth::Target target,
      url::SchemeHostPort origin,
      net::HttpAuth::Scheme scheme,
      std::string realm,
      std::vector<std::string> permitted_paths = {},
      net::NetworkAnonymizationKey network_anonymization_key = {});
  ~HttpAuthProtectionSpace();

  HttpAuthProtectionSpace(const HttpAuthProtectionSpace&);
  HttpAuthProtectionSpace& operator=(const HttpAuthProtectionSpace&);
  HttpAuthProtectionSpace(HttpAuthProtectionSpace&&) noexcept;
  HttpAuthProtectionSpace& operator=(HttpAuthProtectionSpace&&) noexcept;

  // Converts an upstream challenge into a validated Basic/Digest protection
  // space. Invalid, unsupported or non-HTTP(S) challenges return nullopt.
  static std::optional<HttpAuthProtectionSpace> FromChallenge(
      const net::AuthChallengeInfo& auth_info,
      const GURL& request_url,
      const net::NetworkAnonymizationKey& network_anonymization_key = {});

  // Returns true only for server/proxy Basic or Digest spaces on HTTP(S).
  bool IsValid() const;

  // Adds the parent directory of |path| to the protection space, following
  // net::HttpAuthCache's RFC 2617 path handling. Proxy spaces always use an
  // empty path and do not gain a path scope.
  void AddPermittedPath(std::string_view path);

  // Tests whether a request path is in one of the explicitly recorded
  // protection directories. An empty path is never treated as a server root.
  bool AllowsPath(std::string_view path) const;

  // Full protection-space match, including path and NAK.
  bool Matches(const HttpAuthProtectionSpace& other,
               std::string_view request_path) const;

  // Realm match used by the "never save this realm" setting. Path and NAK are
  // intentionally excluded so the decision covers the complete profile-local
  // realm without weakening credential candidate matching.
  bool MatchesRealm(const HttpAuthProtectionSpace& other) const;

  // The signon realm format is kept compatible with LoginHandler so existing
  // Chromium PasswordStore entries remain addressable.
  std::string SignonRealm() const;

  // Returns a URL containing exactly scheme, host and effective port. It never
  // includes a request path and is safe for origin filtering.
  GURL OriginUrl() const;

  // Canonicalizes a request path into the protection directory representation
  // used by Chromium's HttpAuthCache.
  static std::string CanonicalProtectionPath(std::string_view path);

  friend bool operator==(const HttpAuthProtectionSpace& lhs,
                         const HttpAuthProtectionSpace& rhs);

  net::HttpAuth::Target target = net::HttpAuth::AUTH_NONE;
  url::SchemeHostPort origin;
  net::HttpAuth::Scheme scheme = net::HttpAuth::AUTH_SCHEME_MAX;
  // HTTP auth realms are case-sensitive according to the HTTP auth model.
  std::string realm;
  // Paths are canonical parent directories. They are never used for proxy
  // credentials, whose protection space is the whole proxy authority.
  std::vector<std::string> permitted_paths;
  net::NetworkAnonymizationKey network_anonymization_key;
  // A serialized NAK is retained only for metadata loaded from the profile.
  // Non-persistable (nonce/opaque) keys are never written and therefore never
  // reused across browser sessions.
  std::optional<base::Value> network_anonymization_key_metadata_value;
};

// Metadata persisted next to, but never containing, PasswordStore secrets.
// The metadata preference is versioned and can be discarded safely if an
// unknown version is encountered. The password remains exclusively in the
// encrypted Chromium PasswordStore.
struct HttpAuthCredentialMetadata {
  int version = 1;
  HttpAuthProtectionSpace protection_space;
  std::u16string username;
  bool preferred = false;
  base::Time last_successful;

  friend bool operator==(const HttpAuthCredentialMetadata& lhs,
                         const HttpAuthCredentialMetadata& rhs);
};

// A transient result returned by the secure PasswordStore lookup. The
// password is intentionally only present in this in-memory result and is not
// copied into the metadata preference.
struct HttpAuthCredential {
  HttpAuthCredential();
  HttpAuthCredential(HttpAuthCredential&& other) noexcept;
  HttpAuthCredential& operator=(HttpAuthCredential&& other) noexcept;
  HttpAuthCredential(const HttpAuthCredential&) = delete;
  HttpAuthCredential& operator=(const HttpAuthCredential&) = delete;
  ~HttpAuthCredential();

  HttpAuthCredentialMetadata metadata;
  std::u16string password;
};

class HttpAuthCredentialService : public KeyedService {
 public:
  using CredentialsCallback =
      base::OnceCallback<void(std::vector<HttpAuthCredential>)>;
  using UpdateCallback = base::OnceCallback<void(bool)>;

  static constexpr int kCurrentMetadataVersion = 1;

  // Registers the non-secret, profile-scoped metadata preference. This is
  // called from chrome::RegisterProfilePrefs, not lazily at first use.
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // Production constructor. The PasswordStore is the sole persistence
  // authority for usernames/passwords; |profile| supplies profile prefs and
  // the profile lifetime.
  HttpAuthCredentialService(
      Profile* profile,
      scoped_refptr<password_manager::PasswordStoreInterface> password_store);

  // Dependency-injection constructor for deterministic unit tests and for
  // embedders that already own profile prefs. No profile object is retained.
  HttpAuthCredentialService(
      PrefService* prefs,
      scoped_refptr<password_manager::PasswordStoreInterface> password_store,
      bool profile_is_incognito_for_testing = false);

  HttpAuthCredentialService(const HttpAuthCredentialService&) = delete;
  HttpAuthCredentialService& operator=(const HttpAuthCredentialService&) =
      delete;
  ~HttpAuthCredentialService() override;

  // Retrieves exact-realm credentials from Chromium's PasswordStore and
  // ranks them by preferred, last-successful, then username. Automatic
  // lookups require matching persisted protection metadata and path scope.
  // Explicit selection may expose exact-realm entries that predate Ahoi
  // metadata. A never-save decision suppresses future writes but does not hide
  // an already saved account. In incognito only explicit selection is allowed
  // and no write is ever performed.
  void GetCredentials(const HttpAuthProtectionSpace& protection_space,
                      std::string_view request_path,
                      HttpAuthRequestContext request_context,
                      HttpAuthSelectionMode selection_mode,
                      CredentialsCallback callback);

  // Records a credential only after the network layer has confirmed a
  // successful challenge. There is deliberately no save operation for a
  // merely submitted or failed attempt. Unencrypted HTTP additionally
  // requires an explicit UI confirmation before persistence. Incognito always
  // fails closed and invokes |done| without touching the PasswordStore.
  void RecordSuccessfulAuthentication(
      const HttpAuthProtectionSpace& protection_space,
      std::string_view request_path,
      const net::AuthCredentials& credentials,
      HttpAuthRequestContext request_context,
      bool user_confirmed_insecure_http,
      base::OnceClosure done = base::DoNothing());

  // A failed 401/407 is intentionally a no-op. In particular, a single
  // failure must not delete or mutate a saved credential.
  void RecordAuthenticationFailure(
      const HttpAuthProtectionSpace& protection_space,
      std::string_view request_path,
      std::u16string_view username,
      HttpAuthRequestContext request_context);

  // Metadata-only management operations. They never contain or return a
  // password. All return false when the metadata preference is unavailable or
  // malformed, allowing callers to fail closed.
  bool SetPreferredCredential(const HttpAuthProtectionSpace& protection_space,
                              std::u16string_view username,
                              HttpAuthRequestContext request_context);
  bool SetNeverSaveForRealm(const HttpAuthProtectionSpace& protection_space,
                            bool never_save,
                            HttpAuthRequestContext request_context);
  bool IsNeverSaveForRealm(
      const HttpAuthProtectionSpace& protection_space) const;
  std::vector<HttpAuthCredentialMetadata> GetMetadataSnapshot() const;
  std::vector<HttpAuthProtectionSpace> GetNeverSaveSnapshot() const;

  // Deletes one credential from PasswordStore and its corresponding metadata.
  // This is separate from HttpAuthSessionController::SwitchAccount(), which
  // only clears volatile network state. Incognito requests are no-ops.
  void DeleteCredential(const HttpAuthProtectionSpace& protection_space,
                        std::u16string_view username,
                        HttpAuthRequestContext request_context,
                        base::OnceClosure done = base::DoNothing());

  // Updates one exact PasswordStore credential. Username changes use
  // UpdateLoginWithPrimaryKey so no plaintext leaves Chromium's credential
  // store path. The metadata ledger is renamed only after the store callback.
  // Incognito, collisions, missing credentials, and malformed metadata all
  // fail closed with false.
  void UpdateCredential(const HttpAuthProtectionSpace& protection_space,
                        std::u16string_view old_username,
                        std::u16string new_username,
                        std::u16string new_password,
                        HttpAuthRequestContext request_context,
                        UpdateCallback done);

  // Deletes all PasswordStore credentials for the exact realm and its
  // metadata. The PasswordStore remains the only secret store.
  void DeleteRealm(const HttpAuthProtectionSpace& protection_space,
                   HttpAuthRequestContext request_context,
                   base::OnceClosure done = base::DoNothing());

  // Builds a DELETE_MATCHES filter containing exactly one origin and no
  // registrable-domain wildcard. A null result means the protection space is
  // invalid and must not be passed to ClearHttpAuthCache.
  static network::mojom::ClearDataFilterPtr BuildHttpAuthCacheFilter(
      const HttpAuthProtectionSpace& protection_space);
  static network::mojom::ClearDataFilterPtr BuildHttpAuthCacheFilterForOrigin(
      const GURL& origin_url);

 private:
  class CredentialQuery;

  struct PendingQuery {
    std::unique_ptr<CredentialQuery> query;
  };

  void OnCredentialLookupComplete(
      HttpAuthProtectionSpace protection_space,
      std::string request_path,
      HttpAuthRequestContext request_context,
      HttpAuthSelectionMode selection_mode,
      CredentialsCallback callback,
      CredentialQuery* query,
      password_manager::LoginsResultOrError results_or_error);

  void OnSaveLookupComplete(
      HttpAuthProtectionSpace protection_space,
      std::string request_path,
      net::AuthCredentials credentials,
      HttpAuthRequestContext request_context,
      bool user_confirmed_insecure_http,
      base::OnceClosure done,
      CredentialQuery* query,
      password_manager::LoginsResultOrError results_or_error);

  void OnDeleteLookupComplete(
      HttpAuthProtectionSpace protection_space,
      std::optional<std::u16string> username,
      HttpAuthRequestContext request_context,
      base::OnceClosure done,
      CredentialQuery* query,
      password_manager::LoginsResultOrError results_or_error);

  void OnUpdateLookupComplete(
      HttpAuthProtectionSpace protection_space,
      std::u16string old_username,
      std::u16string new_username,
      ScopedHttpAuthSecret new_password,
      HttpAuthRequestContext request_context,
      UpdateCallback done,
      CredentialQuery* query,
      password_manager::LoginsResultOrError results_or_error);

  std::optional<HttpAuthCredentialMetadata> FindMetadata(
      const HttpAuthProtectionSpace& protection_space,
      std::string_view request_path,
      std::u16string_view username) const;

  bool PersistMetadata(
      const std::vector<HttpAuthCredentialMetadata>& metadata) const;
  void RecordMetadataAfterSuccess(
      const HttpAuthProtectionSpace& protection_space,
      std::string_view request_path,
      std::u16string_view username,
      base::Time success_time);
  void DeleteMetadata(const HttpAuthProtectionSpace& protection_space,
                      std::optional<std::u16string_view> username);
  bool CanRenameCredentialMetadata(
      const HttpAuthProtectionSpace& protection_space,
      std::u16string_view old_username,
      std::u16string_view new_username) const;
  bool RenameCredentialMetadata(const HttpAuthProtectionSpace& protection_space,
                                std::u16string_view old_username,
                                std::u16string_view new_username);

  password_manager::PasswordForm MakePasswordForm(
      const HttpAuthProtectionSpace& protection_space,
      const net::AuthCredentials& credentials,
      base::Time now) const;

  bool MatchesStoredCredential(
      const HttpAuthProtectionSpace& protection_space,
      const password_manager::StoredCredential& credential) const;

  void RemovePendingQuery(CredentialQuery* query);

  raw_ptr<PrefService> prefs_ = nullptr;
  scoped_refptr<password_manager::PasswordStoreInterface> password_store_;
  bool profile_is_incognito_for_testing_ = false;
  std::vector<PendingQuery> pending_queries_;
  base::WeakPtrFactory<HttpAuthCredentialService> weak_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_CREDENTIAL_SERVICE_H_
