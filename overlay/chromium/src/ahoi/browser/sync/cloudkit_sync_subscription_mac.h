// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_SUBSCRIPTION_MAC_H_
#define AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_SUBSCRIPTION_MAC_H_

#import <Foundation/Foundation.h>

#include <optional>
#include <string>

namespace ahoi::sync {

// The database subscription a persisted CKSyncEngine state remembers.
//
// On a fresh state CKSyncEngine adopts any existing CKDatabaseSubscription it
// discovers in the private database and saves the configured
// `subscriptionID` only when it finds none. It then keeps that choice in its
// state forever; a later or different configured ID is ignored. A leftover
// test subscription therefore captured every Ahoi engine in the container.
struct EngineSubscriptionState {
  std::string remembered;  // Empty: none remembered yet.
  bool needs_save = false;
};

// Reads the remembered subscription from an archived
// CKSyncEngineStateSerialization without instantiating CloudKit's private
// state class. Nullopt when the archive is not in the one recognized shape.
std::optional<EngineSubscriptionState> ReadEngineSubscription(NSData* archived);

// True when the engine remembers a subscription other than the configured
// one. An empty configuration or an empty memory never migrates.
bool ShouldRebindEngineSubscription(const EngineSubscriptionState& state,
                                    const std::string& configured);

// Returns a copy of `archived` that remembers `subscription_id` and asks the
// engine to save it. Every other byte of meaning (tokens, account, pending
// changes) is kept. The old subscription is neither referenced nor deleted.
// Nil when the archive is not in the recognized shape or the result does not
// read back exactly as intended.
NSData* RebindEngineSubscription(NSData* archived,
                                 const std::string& subscription_id);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_SUBSCRIPTION_MAC_H_
