// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_WORKSPACE_SERVICE_FACTORY_H_
#define AHOI_BROWSER_SESSION_WORKSPACE_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace ahoi {

class WorkspaceService;

// Owns one WorkspaceService for each regular, non-OTR Profile. Workspace
// state is never redirected into the original Profile from an OTR context.
class WorkspaceServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static WorkspaceService* GetForProfile(Profile* profile);
  static WorkspaceServiceFactory* GetInstance();

  WorkspaceServiceFactory(const WorkspaceServiceFactory&) = delete;
  WorkspaceServiceFactory& operator=(const WorkspaceServiceFactory&) = delete;

 private:
  friend base::NoDestructor<WorkspaceServiceFactory>;

  WorkspaceServiceFactory();
  ~WorkspaceServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_SESSION_WORKSPACE_SERVICE_FACTORY_H_
