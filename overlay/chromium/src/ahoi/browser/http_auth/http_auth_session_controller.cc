// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_session_controller.h"

#include <utility>

#include "ahoi/browser/http_auth/http_auth_credential_service_factory.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/mojom/network_context.mojom.h"

namespace ahoi {

HttpAuthSessionController::HttpAuthSessionController(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<HttpAuthSessionController>(*web_contents) {}

HttpAuthSessionController::~HttpAuthSessionController() = default;

// static
HttpAuthSessionController* HttpAuthSessionController::GetOrCreate(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return nullptr;
  }
  HttpAuthSessionController::CreateForWebContents(web_contents);
  return HttpAuthSessionController::FromWebContents(web_contents);
}

void HttpAuthSessionController::RecordSuccessfulAuthentication(
    HttpAuthProtectionSpace protection_space) {
  if (!protection_space.IsValid()) {
    return;
  }
  active_protection_space_ = std::move(protection_space);
}

void HttpAuthSessionController::SwitchAccount(base::OnceClosure done) {
  ClearCacheAndConnections(std::move(done));
}

void HttpAuthSessionController::DeleteSavedCredential(
    std::u16string_view username,
    base::OnceClosure done) {
  Profile* profile =
      web_contents()
          ? Profile::FromBrowserContext(web_contents()->GetBrowserContext())
          : nullptr;
  HttpAuthCredentialService* service =
      HttpAuthCredentialServiceFactory::GetForProfile(profile);
  if (!service || !active_protection_space_ || username.empty()) {
    std::move(done).Run();
    return;
  }
  service->DeleteCredential(*active_protection_space_, username,
                            request_context(), std::move(done));
}

void HttpAuthSessionController::ForgetRealmAndSwitch(base::OnceClosure done) {
  Profile* profile =
      web_contents()
          ? Profile::FromBrowserContext(web_contents()->GetBrowserContext())
          : nullptr;
  HttpAuthCredentialService* service =
      HttpAuthCredentialServiceFactory::GetForProfile(profile);
  if (!service || !active_protection_space_) {
    std::move(done).Run();
    return;
  }
  service->DeleteRealm(
      *active_protection_space_, request_context(),
      base::BindOnce(&HttpAuthSessionController::ClearCacheAndConnections,
                     weak_factory_.GetWeakPtr(), std::move(done)));
}

void HttpAuthSessionController::ClearCacheAndConnections(
    base::OnceClosure done) {
  if (!web_contents()) {
    std::move(done).Run();
    return;
  }
  network::mojom::NetworkContext* network_context =
      web_contents()
          ->GetBrowserContext()
          ->GetDefaultStoragePartition()
          ->GetNetworkContext();
  const GURL filter_url = active_protection_space_
                              ? active_protection_space_->OriginUrl()
                              : web_contents()->GetLastCommittedURL();
  network::mojom::ClearDataFilterPtr filter =
      HttpAuthCredentialService::BuildHttpAuthCacheFilterForOrigin(filter_url);
  if (!network_context || !filter) {
    std::move(done).Run();
    return;
  }
  network_context->ClearHttpAuthCache(
      base::Time::Min(), base::Time::Max(), std::move(filter),
      base::BindOnce(&HttpAuthSessionController::OnAuthCacheCleared,
                     weak_factory_.GetWeakPtr(), std::move(done)));
}

void HttpAuthSessionController::OnAuthCacheCleared(base::OnceClosure done) {
  if (!web_contents()) {
    std::move(done).Run();
    return;
  }
  network::mojom::NetworkContext* network_context =
      web_contents()
          ->GetBrowserContext()
          ->GetDefaultStoragePartition()
          ->GetNetworkContext();
  if (!network_context) {
    std::move(done).Run();
    return;
  }
  network_context->CloseAllConnections(
      base::BindOnce(&HttpAuthSessionController::OnConnectionsClosed,
                     weak_factory_.GetWeakPtr(), std::move(done)));
}

void HttpAuthSessionController::OnConnectionsClosed(base::OnceClosure done) {
  if (web_contents()) {
    web_contents()->GetController().Reload(content::ReloadType::NORMAL,
                                           /*check_for_repost=*/false);
  }
  std::move(done).Run();
}

HttpAuthRequestContext HttpAuthSessionController::request_context() const {
  Profile* profile =
      web_contents()
          ? Profile::FromBrowserContext(web_contents()->GetBrowserContext())
          : nullptr;
  return profile && profile->IsOffTheRecord()
             ? HttpAuthRequestContext::kIncognito
             : HttpAuthRequestContext::kRegular;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(HttpAuthSessionController);

}  // namespace ahoi
