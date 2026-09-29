// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/keychain_sync_bootstrap_mac.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <cmath>
#include <limits>
#include <utility>

#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
#include "base/apple/foundation_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "crypto/secure_util.h"
#include "crypto/sha2.h"

namespace ahoi::sync {
namespace {

using Result = KeychainBootstrapResult;
constexpr NSUInteger kKeyBytes = 32;
enum class ItemKind { kCanonical, kPending, kJournal };

struct Journal {
  uint32_t key_version = 0;
  bool generated = false;
  std::optional<std::string> accepted_tag;
  friend bool operator==(const Journal&, const Journal&) = default;
};

struct Family {
  NSData* canonical = nil;
  NSData* pending = nil;
  std::optional<Journal> journal;
};

class GeneratedKey final {
 public:
  GeneratedKey() : data_([NSMutableData dataWithLength:kKeyBytes]) {}
  ~GeneratedKey() {
    crypto::SecureZeroBuffer(base::apple::NSMutableDataToSpan(data_));
  }
  NSMutableData* data() const { return data_; }

 private:
  NSMutableData* data_;
};

Result Status(OSStatus status) {
  switch (status) {
    case errSecSuccess:
      return Result::kOk;
    case errSecItemNotFound:
      return Result::kMissing;
    case errSecUserCanceled:
      return Result::kCancelled;
    case errSecDecode:
      return Result::kCorrupt;
    case errSecDuplicateItem:
      return Result::kConflict;
    default:
      return Result::kUnavailable;
  }
}

bool ValidTag(std::string_view value) {
  return !value.empty() && base::IsStringUTF8(value);
}

bool SameKey(NSData* first, NSData* second) {
  return first && second && first.length == kKeyBytes &&
         second.length == kKeyBytes &&
         crypto::SecureMemEqual(base::apple::NSDataToSpan(first),
                                base::apple::NSDataToSpan(second));
}

std::string KeyHash(NSData* key) {
  return base::ToLowerASCII(
      base::HexEncode(crypto::SHA256Hash(base::apple::NSDataToSpan(key))));
}

bool ReadVersion(id object, uint32_t* version) {
  if (![object isKindOfClass:[NSNumber class]] ||
      CFGetTypeID((__bridge CFTypeRef)object) == CFBooleanGetTypeID()) {
    return false;
  }
  const double value = [static_cast<NSNumber*>(object) doubleValue];
  if (!std::isfinite(value) || value < 1 ||
      value > std::numeric_limits<uint32_t>::max() ||
      std::floor(value) != value) {
    return false;
  }
  *version = static_cast<uint32_t>(value);
  return true;
}

Result DecodeJournal(NSData* data,
                     uint32_t expected_version,
                     Journal* journal) {
  NSError* error = nil;
  id root = [NSJSONSerialization JSONObjectWithData:data
                                            options:0
                                              error:&error];
  if (error || ![root isKindOfClass:[NSDictionary class]]) {
    return Result::kCorrupt;
  }
  NSDictionary* value = static_cast<NSDictionary*>(root);
  const bool has_receipt = value[@"acceptedReceipt"] != nil;
  id origin = value[@"origin"];
  if (value.count != (has_receipt ? 3u : 2u) ||
      !ReadVersion(value[@"keyVersion"], &journal->key_version) ||
      journal->key_version != expected_version ||
      ![origin isKindOfClass:[NSString class]] ||
      (![origin isEqualToString:@"generated"] &&
       ![origin isEqualToString:@"externallyProvisioned"])) {
    return Result::kCorrupt;
  }
  journal->generated = [origin isEqualToString:@"generated"];
  journal->accepted_tag.reset();
  if (has_receipt) {
    id receipt_object = value[@"acceptedReceipt"];
    if (![receipt_object isKindOfClass:[NSDictionary class]]) {
      return Result::kCorrupt;
    }
    NSDictionary* receipt = static_cast<NSDictionary*>(receipt_object);
    uint32_t receipt_version = 0;
    id tag = receipt[@"serverChangeTag"];
    if (receipt.count != 2 ||
        !ReadVersion(receipt[@"keyVersion"], &receipt_version) ||
        receipt_version != expected_version ||
        ![tag isKindOfClass:[NSString class]]) {
      return Result::kCorrupt;
    }
    std::string change_tag = base::SysNSStringToUTF8(tag);
    if (!ValidTag(change_tag)) {
      return Result::kCorrupt;
    }
    journal->accepted_tag = std::move(change_tag);
  }
  return Result::kOk;
}

NSData* EncodeJournal(const Journal& journal) {
  NSMutableDictionary* value = [@{
    @"keyVersion" : @(journal.key_version),
    @"origin" : journal.generated ? @"generated" : @"externallyProvisioned",
  } mutableCopy];
  if (journal.accepted_tag) {
    value[@"acceptedReceipt"] = @{
      @"keyVersion" : @(journal.key_version),
      @"serverChangeTag" : base::SysUTF8ToNSString(*journal.accepted_tag),
    };
  }
  return [NSJSONSerialization dataWithJSONObject:value
                                         options:NSJSONWritingSortedKeys
                                           error:nil];
}

void FillState(const Family& family, KeychainBootstrapState* state) {
  state->canonical_key_present = family.canonical != nil;
  state->candidate_present =
      family.pending != nil || family.journal.has_value();
  state->uses_existing_key = family.journal && !family.journal->generated;
  if (family.journal) {
    state->accepted_change_tag = family.journal->accepted_tag;
  }
  if (family.canonical) {
    state->canonical_sha256 = KeyHash(family.canonical);
  }
  NSData* candidate =
      state->uses_existing_key ? family.canonical : family.pending;
  if (candidate) {
    state->candidate_sha256 = KeyHash(candidate);
  }
}

}  // namespace

class KeychainSyncBootstrapMac::Impl {
 public:
  Impl(const CloudKitSyncConfigurationMac& configuration,
       SyncAuthorization authorization)
      : configuration_(configuration),
        authorization_(std::move(authorization)) {}

  Result ReadState(KeychainBootstrapState* state) const {
    if (!state) {
      return Result::kCorrupt;
    }
    *state = {};
    Family family;
    const Result result = ReadFamily(&family);
    if (result == Result::kOk) {
      FillState(family, state);
    }
    return result;
  }

  Result PrepareCandidate(KeychainBootstrapState* state) const {
    if (!state) {
      return Result::kCorrupt;
    }
    *state = {};
    Family family;
    Result result = ReadFamily(&family);
    if (result != Result::kOk && result != Result::kMissing) {
      return result;
    }
    if (family.journal) {
      FillState(family, state);
      return Result::kOk;
    }
    if (!family.canonical && !family.pending) {
      GeneratedKey generated;
      if (!generated.data() ||
          SecRandomCopyBytes(kSecRandomDefault, kKeyBytes,
                             generated.data().mutableBytes) != errSecSuccess) {
        return Result::kUnavailable;
      }
      result = AddItem(ItemKind::kPending, generated.data());
      if (result != Result::kOk && result != Result::kConflict) {
        return result;
      }
      // A duplicate is not an overwrite or permission to adopt other bytes.
      result = VerifyKey(ItemKind::kPending, generated.data());
      if (result != Result::kOk) {
        return result;
      }
    }
    Journal journal{.key_version = configuration_.key_version,
                    .generated = family.canonical == nil};
    result = AddJournal(journal);
    return result == Result::kOk ? ReadState(state) : result;
  }

  Result RecordAcceptedClaim(std::string_view change_tag) const {
    if (!ValidTag(change_tag)) {
      return Result::kCorrupt;
    }
    Family family;
    Result result = ReadFamily(&family);
    if (result != Result::kOk) {
      return result;
    }
    if (!family.journal) {
      return Result::kMissing;
    }
    Journal journal = *family.journal;
    if (journal.accepted_tag) {
      return *journal.accepted_tag == change_tag ? Result::kOk
                                                 : Result::kConflict;
    }
    journal.accepted_tag = std::string(change_tag);
    NSData* data = EncodeJournal(journal);
    if (!data) {
      return Result::kCorrupt;
    }
    NSMutableDictionary* query = Query(ItemKind::kJournal);
    NSDictionary* attributes = @{(__bridge NSString*)kSecValueData : data};
    result = CheckAccess();
    if (result != Result::kOk) {
      return result;
    }
    result = Status(SecItemUpdate((__bridge CFDictionaryRef)query,
                                  (__bridge CFDictionaryRef)attributes));
    if (result != Result::kOk) {
      return result == Result::kMissing ? Result::kConflict : result;
    }
    return VerifyJournal(journal);
  }

  Result PromoteAcceptedCandidate(std::string_view matching_change_tag) const {
    if (!ValidTag(matching_change_tag)) {
      return Result::kCorrupt;
    }
    Family family;
    Result result = ReadFamily(&family);
    if (result != Result::kOk) {
      return result;
    }
    if (!family.journal) {
      return Result::kMissing;
    }
    if (!family.journal->accepted_tag ||
        *family.journal->accepted_tag != matching_change_tag) {
      return Result::kConflict;
    }
    NSData* expected =
        family.journal->generated ? family.pending : family.canonical;
    if (family.journal->generated) {
      result = AddItem(ItemKind::kCanonical, expected);
      if (result != Result::kOk && result != Result::kConflict) {
        return result;
      }
    }
    result = VerifyKey(ItemKind::kCanonical, expected);
    if (result != Result::kOk) {
      return result;
    }
    result = VerifyJournal(*family.journal);
    if (result != Result::kOk) {
      return result;
    }
    // Canonical is now verified against the actual accepted candidate. Remove
    // the journal FIRST: a crash then leaves a canonical/pending pair that can
    // still be compared, not an accepted journal with no matching key proof.
    result = DeleteItem(ItemKind::kJournal);
    if (result != Result::kOk) {
      return result;
    }
    if (family.pending) {
      result = VerifyKey(ItemKind::kPending, family.pending);
      if (result != Result::kOk && result != Result::kMissing) {
        return result;
      }
      result = DeleteItem(ItemKind::kPending);
      if (result != Result::kOk) {
        return result;
      }
    }
    return VerifyKey(ItemKind::kCanonical, expected);
  }

  Result DiscardUnacceptedCandidate() const {
    // Deliberately independent of canonical validity/equality: a losing local
    // unaccepted candidate can be discarded without touching the winner's key.
    std::optional<Journal> journal;
    Result result = ReadJournal(&journal);
    if (result != Result::kOk && result != Result::kMissing) {
      return result;
    }
    if (journal && journal->accepted_tag) {
      return Result::kConflict;
    }
    NSData* pending = nil;
    result = ReadItem(ItemKind::kPending, &pending);
    if (result != Result::kOk && result != Result::kMissing) {
      return result;
    }
    if (pending && pending.length != kKeyBytes) {
      return Result::kCorrupt;
    }
    if (journal) {
      // The lock excludes a concurrent local acceptance between this verified
      // unaccepted journal and its deletion. Every mutation rechecks authority.
      result = DeleteItem(ItemKind::kJournal);
      if (result != Result::kOk) {
        return result;
      }
    }
    return pending ? DeleteItem(ItemKind::kPending) : CheckAccess();
  }

 private:
  Result CheckAccess() const {
    if (!configuration_.IsE2EKeyConfigured() ||
        !base::IsStringUTF8(configuration_.keychain_service) ||
        !base::IsStringUTF8(configuration_.keychain_account) ||
        !base::IsStringUTF8(configuration_.keychain_access_group)) {
      return Result::kUnavailable;
    }
    return authorization_ && authorization_.Run() ? Result::kOk
                                                  : Result::kCancelled;
  }

  NSMutableDictionary* Query(ItemKind kind) const {
    std::string account = configuration_.keychain_account;
    if (kind != ItemKind::kCanonical) {
      account += kind == ItemKind::kPending ? ".bootstrap-pending.v"
                                            : ".bootstrap-journal.v";
      account += base::NumberToString(configuration_.key_version);
    }
    NSMutableDictionary* query = [@{
      (__bridge NSString*)kSecClass : (__bridge id)kSecClassGenericPassword,
      (__bridge NSString*)kSecAttrService :
          base::SysUTF8ToNSString(configuration_.keychain_service),
      (__bridge NSString*)kSecAttrAccount : base::SysUTF8ToNSString(account),
      (__bridge NSString*)kSecUseDataProtectionKeychain : @YES,
      (__bridge NSString*)
      kSecAttrSynchronizable : @(kind == ItemKind::kCanonical),
    } mutableCopy];
    if (!configuration_.keychain_access_group.empty()) {
      query[(__bridge NSString*)kSecAttrAccessGroup] =
          base::SysUTF8ToNSString(configuration_.keychain_access_group);
    }
    return query;
  }

  Result ReadItem(ItemKind kind, NSData* __strong* data) const {
    *data = nil;
    Result result = CheckAccess();
    if (result != Result::kOk) {
      return result;
    }
    NSMutableDictionary* query = Query(kind);
    query[(__bridge NSString*)kSecReturnData] = @YES;
    query[(__bridge NSString*)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
    CFTypeRef raw = nullptr;
    result = Status(SecItemCopyMatching((__bridge CFDictionaryRef)query, &raw));
    id value = CFBridgingRelease(raw);
    const Result access = CheckAccess();
    if (access != Result::kOk) {
      return access;
    }
    if (result != Result::kOk) {
      return result;
    }
    if (![value isKindOfClass:[NSData class]]) {
      return Result::kCorrupt;
    }
    *data = static_cast<NSData*>(value);
    return Result::kOk;
  }

  Result ReadJournal(std::optional<Journal>* journal) const {
    journal->reset();
    NSData* data = nil;
    const Result result = ReadItem(ItemKind::kJournal, &data);
    if (result != Result::kOk) {
      return result;
    }
    Journal decoded;
    const Result decoded_result =
        DecodeJournal(data, configuration_.key_version, &decoded);
    if (decoded_result == Result::kOk) {
      *journal = std::move(decoded);
    }
    return decoded_result;
  }

  Result ReadFamily(Family* family) const {
    for (const auto kind : {ItemKind::kCanonical, ItemKind::kPending}) {
      NSData* data = nil;
      const Result result = ReadItem(kind, &data);
      if (result != Result::kOk && result != Result::kMissing) {
        return result;
      }
      if (data && data.length != kKeyBytes) {
        return Result::kCorrupt;
      }
      if (kind == ItemKind::kCanonical) {
        family->canonical = data;
      } else {
        family->pending = data;
      }
    }
    const Result result = ReadJournal(&family->journal);
    if (result != Result::kOk && result != Result::kMissing) {
      return result;
    }
    if (family->canonical && family->pending &&
        !SameKey(family->canonical, family->pending)) {
      return Result::kConflict;
    }
    if (family->journal &&
        (family->journal->generated ? !family->pending : !family->canonical)) {
      // Never infer the missing candidate's bytes from an unbound canonical
      // key. An old generated journal alone remains explicit recovery state.
      return Result::kCorrupt;
    }
    return family->canonical || family->pending || family->journal
               ? Result::kOk
               : Result::kMissing;
  }

  Result AddItem(ItemKind kind, NSData* data) const {
    NSMutableDictionary* query = Query(kind);
    query[(__bridge NSString*)kSecValueData] = data;
    query[(__bridge NSString*)kSecAttrAccessible] =
        kind == ItemKind::kCanonical
            ? (__bridge id)kSecAttrAccessibleAfterFirstUnlock
            : (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
    const Result access = CheckAccess();
    if (access != Result::kOk) {
      return access;
    }
    // No update fallback. Duplicate items must be read and compared by caller.
    return Status(SecItemAdd((__bridge CFDictionaryRef)query, nullptr));
  }

  Result DeleteItem(ItemKind kind) const {
    if (kind == ItemKind::kCanonical) {
      return Result::kConflict;
    }
    NSMutableDictionary* query = Query(kind);
    const Result access = CheckAccess();
    if (access != Result::kOk) {
      return access;
    }
    const Result result =
        Status(SecItemDelete((__bridge CFDictionaryRef)query));
    if (result != Result::kOk && result != Result::kMissing) {
      return result;
    }
    NSData* readback = nil;
    const Result read = ReadItem(kind, &readback);
    return read == Result::kMissing ? Result::kOk
           : read == Result::kOk    ? Result::kConflict
                                    : read;
  }

  Result VerifyKey(ItemKind kind, NSData* expected) const {
    NSData* actual = nil;
    const Result result = ReadItem(kind, &actual);
    if (result != Result::kOk) {
      return result;
    }
    if (actual.length != kKeyBytes) {
      return Result::kCorrupt;
    }
    return SameKey(actual, expected) ? Result::kOk : Result::kConflict;
  }

  Result VerifyJournal(const Journal& expected) const {
    std::optional<Journal> actual;
    const Result result = ReadJournal(&actual);
    if (result != Result::kOk) {
      return result == Result::kMissing ? Result::kConflict : result;
    }
    return *actual == expected ? Result::kOk : Result::kConflict;
  }

  Result AddJournal(const Journal& journal) const {
    NSData* data = EncodeJournal(journal);
    if (!data) {
      return Result::kCorrupt;
    }
    const Result result = AddItem(ItemKind::kJournal, data);
    if (result != Result::kOk && result != Result::kConflict) {
      return result;
    }
    return VerifyJournal(journal);
  }

  const CloudKitSyncConfigurationMac configuration_;
  const SyncAuthorization authorization_;
};

KeychainSyncBootstrapMac::KeychainSyncBootstrapMac(
    const CloudKitSyncConfigurationMac& configuration,
    SyncAuthorization authorization)
    : impl_(std::make_unique<Impl>(configuration, std::move(authorization))) {}

KeychainSyncBootstrapMac::~KeychainSyncBootstrapMac() = default;

Result KeychainSyncBootstrapMac::ReadState(KeychainBootstrapState* state) {
  @autoreleasepool {
    return impl_->ReadState(state);
  }
}

Result KeychainSyncBootstrapMac::PrepareCandidate(
    KeychainBootstrapState* state) {
  @autoreleasepool {
    return impl_->PrepareCandidate(state);
  }
}

Result KeychainSyncBootstrapMac::RecordAcceptedClaim(
    std::string_view change_tag) {
  @autoreleasepool {
    return impl_->RecordAcceptedClaim(change_tag);
  }
}

Result KeychainSyncBootstrapMac::PromoteAcceptedCandidate(
    std::string_view matching_change_tag) {
  @autoreleasepool {
    return impl_->PromoteAcceptedCandidate(matching_change_tag);
  }
}

Result KeychainSyncBootstrapMac::DiscardUnacceptedCandidate() {
  @autoreleasepool {
    return impl_->DiscardUnacceptedCandidate();
  }
}

}  // namespace ahoi::sync
