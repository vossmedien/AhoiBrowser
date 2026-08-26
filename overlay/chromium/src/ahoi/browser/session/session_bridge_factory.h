// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_SESSION_BRIDGE_FACTORY_H_
#define AHOI_BROWSER_SESSION_SESSION_BRIDGE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace ahoi {

class SessionBridge;

// Eagerly creates the regular-profile bridge after its Browser and Workspace
// dependencies. ProfileSelections::kOriginalOnly is intentional: an OTR
// request returns nullptr rather than observing or mutating the original
// profile's runtime maps.
class SessionBridgeFactory : public ProfileKeyedServiceFactory {
 public:
  static SessionBridge* GetForProfile(Profile* profile);
  static SessionBridgeFactory* GetInstance();

  SessionBridgeFactory(const SessionBridgeFactory&) = delete;
  SessionBridgeFactory& operator=(const SessionBridgeFactory&) = delete;

 private:
  friend base::NoDestructor<SessionBridgeFactory>;

  SessionBridgeFactory();
  ~SessionBridgeFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_SESSION_SESSION_BRIDGE_FACTORY_H_
