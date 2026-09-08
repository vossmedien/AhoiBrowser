// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/cloudkit_sync_key_bootstrap_mac.h"

#import <CloudKit/CloudKit.h>
#import <Foundation/Foundation.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <utility>

#include "ahoi/browser/sync/cloudkit_sync_util_mac.h"
#include "ahoi/browser/sync/keychain_sync_bootstrap_mac.h"
#include "ahoi/browser/sync/keychain_sync_key_mac.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "crypto/sha2.h"

namespace ahoi::sync {
namespace {
NSString* const kClaimType = @"AhoiKeyBootstrapClaim";
NSString* const kClaimName = @"payload-key-bootstrap-v1";

struct Claim {
  uint32_t version;
  std::string change_tag;
  std::string key_sha256;
};

std::optional<Claim> DecodeClaim(CKRecord* record, CKRecordZoneID* zone) {
  NSNumber* number = record[@"keyVersion"];
  NSString* digest = record[@"keySHA256"];
  if (![record.recordType isEqual:kClaimType] ||
      ![record.recordID.recordName isEqual:kClaimName] ||
      ![record.recordID.zoneID isEqual:zone] ||
      ![number isKindOfClass:[NSNumber class]] ||
      CFGetTypeID((__bridge CFTypeRef)number) == CFBooleanGetTypeID() ||
      ![digest isKindOfClass:[NSString class]] || digest.length != 64 ||
      record.recordChangeTag.length == 0) {
    return std::nullopt;
  }
  const double value = number.doubleValue;
  const std::string sha = ToString(digest);
  if (!std::isfinite(value) || value != std::trunc(value) || value <= 0 ||
      value > std::numeric_limits<uint32_t>::max() ||
      sha.find_first_not_of("0123456789abcdef") != std::string::npos) {
    return std::nullopt;
  }
  return Claim{static_cast<uint32_t>(value), ToString(record.recordChangeTag),
               sha};
}

// fcntl locks are per-process: an in-process registry also prevents a second
// profile from opening/closing this inode and releasing the first lock. The
// OS releases the file lock on a crash; no TTL, stale PID or permanent lock
// row.
class KeyFamilyLock {
 public:
  ~KeyFamilyLock() { Release(); }
  bool Acquire(const CloudKitSyncConfigurationMac& config) {
    if (!name_.empty()) {
      return true;
    }
    auto& registry = Registry();
    base::AutoLock guard(registry.lock);
    const auto material = config.keychain_access_group + "\n" +
                          config.keychain_service + "\n" +
                          config.keychain_account;
    const auto name =
        base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(material)));
    if (registry.held.contains(name)) {
      return false;
    }
    NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(
        NSCachesDirectory, NSUserDomainMask, YES);
    if (paths.count == 0) {
      return false;
    }
    const auto directory =
        base::FilePath(paths.firstObject.fileSystemRepresentation)
            .AppendASCII("AhoiBrowser")
            .AppendASCII("SyncBootstrap");
    if (!base::CreateDirectory(directory)) {
      return false;
    }
    base::File file(directory.AppendASCII(name + ".lock"),
                    base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_READ |
                        base::File::FLAG_WRITE);
    if (!file.IsValid() ||
        file.Lock(base::File::LockMode::kExclusive) != base::File::FILE_OK) {
      return false;
    }
    registry.held.insert(name);
    name_ = name;
    file_ = std::move(file);
    return true;
  }
  void Release() {
    if (name_.empty()) {
      return;
    }
    auto& registry = Registry();
    base::AutoLock guard(registry.lock);
    file_.Close();
    registry.held.erase(std::exchange(name_, {}));
  }

 private:
  struct State {
    base::Lock lock;
    std::set<std::string> held;
  };
  static State& Registry() {
    static base::NoDestructor<State> state;
    return *state;
  }
  std::string name_;
  base::File file_;
};

struct Scan {
  std::optional<Claim> claim;
  bool has_data = false;
  bool zone_missing = false;
  bool failed = false;
};

}  // namespace

bool IsCloudKitKeyBootstrapRecord(CKRecord* record) {
  return [record.recordType isEqual:kClaimType] ||
         [record.recordID.recordName isEqual:kClaimName];
}

bool MatchesCloudKitKeyBootstrapRecord(CKRecord* record,
                                       CKRecordZoneID* zone,
                                       uint32_t version,
                                       const std::string& sha) {
  const auto claim = DecodeClaim(record, zone);
  return claim && claim->version == version && claim->key_sha256 == sha;
}

class CloudKitSyncKeyBootstrapMac::Core
    : public std::enable_shared_from_this<Core> {
 public:
  Core(CloudKitSyncConfigurationMac configuration,
       SyncAuthorization original,
       Completion completion)
      : configuration_(std::move(configuration)),
        original_(std::move(original)),
        completion_(std::move(completion)),
        runner_(base::SequencedTaskRunner::GetCurrentDefault()),
        authorization_(base::BindRepeating(
            [](SyncAuthorization original,
               std::shared_ptr<std::atomic<bool>> cancelled) {
              return !cancelled->load(std::memory_order_acquire) && original &&
                     original.Run();
            },
            original_,
            cancelled_)),
        keys_(configuration_, authorization_) {}

  void Start() {
    runner_->PostTask(
        FROM_HERE,
        base::BindOnce([](std::shared_ptr<Core> core) { core->Begin(); },
                       shared_from_this()));
  }
  ~Core() { Stop(); }
  void Stop() {
    cancelled_->store(true, std::memory_order_release);
    [operation_ cancel];
    operation_ = nil;
    if (account_observer_) {
      [[NSNotificationCenter defaultCenter] removeObserver:account_observer_];
      account_observer_ = nil;
    }
    family_lock_.Release();
  }
  bool pending() const { return pending_; }
  void CheckWaitingKey() {
    if (waiting_ && !pending_ && authorization_.Run()) {
      const auto lifetime = shared_from_this();
      TryWaitingKey();  // Caller and state stay on the backend owner sequence.
    }
  }

 private:
  void Begin() {
    if (!authorization_.Run()) {
      Finish("key_setup_cancelled");
      return;
    }
    if (!configuration_.IsTransportConfigured() ||
        !configuration_.IsE2EKeyConfigured()) {
      Finish("key_setup_configuration_missing");
      return;
    }
    if (!family_lock_.Acquire(configuration_)) {
      Finish("key_setup_busy");
      return;
    }
    container_ = [CKContainer
        containerWithIdentifier:ToNSString(
                                    configuration_.container_identifier)];
    zone_ = [[CKRecordZoneID alloc]
        initWithZoneName:ToNSString(configuration_.zone_name)
               ownerName:CKCurrentUserDefaultName];
    const std::weak_ptr<Core> weak = weak_from_this();
    const auto runner = runner_;
    const auto cancelled = cancelled_;
    account_observer_ = [[NSNotificationCenter defaultCenter]
        addObserverForName:CKAccountChangedNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification* notification) {
                  cancelled->store(true, std::memory_order_release);
                  runner->PostTask(
                      FROM_HERE,
                      base::BindOnce(
                          [](std::weak_ptr<Core> weak) {
                            if (auto core = weak.lock()) {
                              core->Finish("key_setup_account_changed");
                            }
                          },
                          weak));
                }];
    [container_ accountStatusWithCompletionHandler:^(CKAccountStatus status,
                                                     NSError* error) {
      runner->PostTask(FROM_HERE,
                       base::BindOnce(
                           [](std::weak_ptr<Core> weak, bool available) {
                             if (auto core = weak.lock()) {
                               if (available) {
                                 core->VerifyAccount(false);
                               } else {
                                 core->Finish("key_setup_account_unavailable");
                               }
                             }
                           },
                           weak, status == CKAccountStatusAvailable && !error));
    }];
  }

  void VerifyAccount(bool final) {
    if (!authorization_.Run()) {
      Finish("key_setup_cancelled");
      return;
    }
    const std::weak_ptr<Core> weak = weak_from_this();
    const auto runner = runner_;
    [container_ fetchUserRecordIDWithCompletionHandler:^(CKRecordID* identity,
                                                         NSError* error) {
      runner->PostTask(FROM_HERE, base::BindOnce(
                                      [](std::weak_ptr<Core> weak, bool final,
                                         CKRecordID* identity, NSError* error) {
                                        if (auto core = weak.lock()) {
                                          core->OnAccount(final, identity,
                                                          error);
                                        }
                                      },
                                      weak, final, identity, error));
    }];
  }

  void OnAccount(bool final, CKRecordID* identity, NSError* error) {
    if (!authorization_.Run() || error || !identity) {
      Finish("key_setup_account_unavailable");
      return;
    }
    if (identity_ && ![identity_ isEqual:identity]) {
      Finish("key_setup_account_changed");
      return;
    }
    identity_ = identity;
    if (final) {
      CompleteVerifiedKey();
    } else {
      ScanRemote();
    }
  }

  void ScanRemote() {
    if (!authorization_.Run()) {
      Finish("key_setup_cancelled");
      return;
    }
    auto scan = std::make_shared<Scan>();
    auto options = [[CKFetchRecordZoneChangesConfiguration alloc] init];
    // Metadata only. Domain ciphertext is fetched by the existing provider,
    // never duplicated/decrypted as part of key setup.
    options.desiredKeys = @[ @"keyVersion", @"keySHA256" ];
    auto operation = [[CKFetchRecordZoneChangesOperation alloc]
               initWithRecordZoneIDs:@[ zone_ ]
        configurationsByRecordZoneID:@{zone_ : options}];
    operation.fetchAllChanges = YES;
    CKRecordZoneID* zone = zone_;
    operation.recordWasChangedBlock =
        ^(CKRecordID* record_id, CKRecord* record, NSError* error) {
          if (error || !record) {
            scan->failed = true;
            return;
          }
          if (IsCloudKitKeyBootstrapRecord(record)) {
            auto claim = DecodeClaim(record, zone);
            if (!claim || scan->claim) {
              scan->failed = true;
            } else {
              scan->claim = std::move(claim);
            }
          } else {
            scan->has_data = true;
          }
        };
    operation.recordZoneFetchCompletionBlock =
        ^(CKRecordZoneID* zone_id, CKServerChangeToken* token, NSData* client,
          BOOL more, NSError* error) {
          if (error.code == CKErrorZoneNotFound ||
              error.code == CKErrorUserDeletedZone) {
            scan->zone_missing = true;
          } else if (error) {
            scan->failed = true;
          }
        };
    const std::weak_ptr<Core> weak = weak_from_this();
    const auto runner = runner_;
    operation.fetchRecordZoneChangesCompletionBlock = ^(NSError* error) {
      runner->PostTask(FROM_HERE,
                       base::BindOnce(
                           [](std::weak_ptr<Core> weak,
                              std::shared_ptr<Scan> scan, NSError* error) {
                             if (auto core = weak.lock()) {
                               core->OnScan(*scan, error);
                             }
                           },
                           weak, scan, error));
    };
    operation_ = operation;
    [container_.privateCloudDatabase addOperation:operation];
  }

  void OnScan(const Scan& scan, NSError* error) {
    operation_ = nil;
    if (!authorization_.Run()) {
      Finish("key_setup_cancelled");
      return;
    }
    if (scan.failed || (error && !scan.zone_missing)) {
      Finish("key_setup_fetch_failed");
      return;
    }
    KeychainBootstrapState state;
    const auto key_state = keys_.ReadState(&state);
    if (key_state != KeychainBootstrapResult::kOk &&
        key_state != KeychainBootstrapResult::kMissing) {
      Finish("key_setup_keychain_recovery");
      return;
    }
    if (scan.claim) {
      claim_ = scan.claim;
      if (claim_->version != configuration_.key_version) {
        Finish("key_setup_version_mismatch");
        return;
      }
      if (state.canonical_sha256 == claim_->key_sha256 ||
          (state.accepted_change_tag == claim_->change_tag &&
           state.candidate_sha256 == claim_->key_sha256)) {
        VerifyAccount(true);
      } else if (state.canonical_key_present || state.accepted_change_tag) {
        Finish("key_setup_key_mismatch");
      } else if (state.candidate_present) {
        // A previous indeterminate save has no creator receipt. Preserve it;
        // do not claim an unrelated candidate as the remote winner.
        Finish("key_setup_receipt_required");
      } else {
        Finish("key_setup_waiting_for_key", true);
      }
      return;
    }
    if (scan.has_data || state.canonical_key_present ||
        state.accepted_change_tag) {
      Finish("key_setup_unclaimed_existing_data");
      return;
    }
    if (scan.zone_missing) {
      if (created_zone_) {
        Finish("key_setup_zone_unavailable");
        return;
      }
      CreateZone();
      return;
    }
    if (keys_.PrepareCandidate(&state) != KeychainBootstrapResult::kOk ||
        !state.candidate_sha256 || state.uses_existing_key) {
      Finish("key_setup_candidate_unavailable");
      return;
    }
    CreateClaim(*state.candidate_sha256);
  }

  void CreateZone() {
    if (!authorization_.Run()) {
      Finish("key_setup_cancelled");
      return;
    }
    created_zone_ = true;
    auto operation = [[CKModifyRecordZonesOperation alloc]
        initWithRecordZonesToSave:@[ [[CKRecordZone alloc]
                                      initWithZoneID:zone_] ]
            recordZoneIDsToDelete:nil];
    const std::weak_ptr<Core> weak = weak_from_this();
    const auto runner = runner_;
    operation.modifyRecordZonesCompletionBlock =
        ^(NSArray<CKRecordZone*>* saved, NSArray<CKRecordZoneID*>* deleted,
          NSError* error) {
          runner->PostTask(
              FROM_HERE,
              base::BindOnce(
                  [](std::weak_ptr<Core> weak, bool saved, NSError* error) {
                    if (auto core = weak.lock()) {
                      core->operation_ = nil;
                      if (error || !saved) {
                        core->Finish("key_setup_zone_unavailable");
                      } else {
                        core->ScanRemote();
                      }
                    }
                  },
                  weak, saved.count == 1, error));
        };
    operation_ = operation;
    [container_.privateCloudDatabase addOperation:operation];
  }

  void CreateClaim(const std::string& digest) {
    if (!authorization_.Run()) {
      Finish("key_setup_cancelled");
      return;
    }
    CKRecord* claim = [[CKRecord alloc]
        initWithRecordType:kClaimType
                  recordID:[[CKRecordID alloc] initWithRecordName:kClaimName
                                                           zoneID:zone_]];
    claim[@"keyVersion"] = @(configuration_.key_version);
    claim[@"keySHA256"] = ToNSString(digest);
    auto operation =
        [[CKModifyRecordsOperation alloc] initWithRecordsToSave:@[ claim ]
                                              recordIDsToDelete:nil];
    operation.savePolicy = CKRecordSaveIfServerRecordUnchanged;
    operation.atomic = YES;
    const std::weak_ptr<Core> weak = weak_from_this();
    const auto runner = runner_;
    operation.modifyRecordsCompletionBlock =
        ^(NSArray<CKRecord*>* saved, NSArray<CKRecordID*>* deleted,
          NSError* error) {
          runner->PostTask(FROM_HERE,
                           base::BindOnce(
                               [](std::weak_ptr<Core> weak,
                                  NSArray<CKRecord*>* saved, NSError* error) {
                                 if (auto core = weak.lock()) {
                                   core->OnClaimSaved(saved, error);
                                 }
                               },
                               weak, saved, error));
        };
    operation_ = operation;
    [container_.privateCloudDatabase addOperation:operation];
  }

  void OnClaimSaved(NSArray<CKRecord*>* saved, NSError* error) {
    operation_ = nil;
    if (!authorization_.Run()) {
      Finish("key_setup_cancelled");
      return;
    }
    CKRecordID* id = [[CKRecordID alloc] initWithRecordName:kClaimName
                                                     zoneID:zone_];
    NSError* specific = error;
    if (error.code == CKErrorPartialFailure) {
      specific = error.userInfo[CKPartialErrorsByItemIDKey][id];
    }
    if (specific.code == CKErrorServerRecordChanged) {
      if (keys_.DiscardUnacceptedCandidate() != KeychainBootstrapResult::kOk) {
        Finish("key_setup_receipt_required");
        return;
      }
      // The losing key is never synchronized. Inspect the actual winner and
      // wait for its canonical item through Apple's synchronizable Keychain.
      ScanRemote();
      return;
    }
    if (error || saved.count != 1) {
      Finish("key_setup_claim_indeterminate");
      return;
    }
    auto claim = DecodeClaim(saved.firstObject, zone_);
    KeychainBootstrapState state;
    if (!claim || claim->version != configuration_.key_version ||
        keys_.ReadState(&state) != KeychainBootstrapResult::kOk ||
        state.candidate_sha256 != claim->key_sha256 ||
        keys_.RecordAcceptedClaim(claim->change_tag) !=
            KeychainBootstrapResult::kOk) {
      Finish("key_setup_receipt_required");
      return;
    }
    // A fresh metadata scan uses no old change token. Promotion needs the
    // persisted accepted receipt AND the actual current server commitment.
    ScanRemote();
  }

  void TryWaitingKey() {
    if (!waiting_ || pending_ || !claim_ || !authorization_.Run()) {
      return;
    }
    if (!family_lock_.Acquire(configuration_)) {
      return;
    }
    KeychainBootstrapState state;
    const auto key_state = keys_.ReadState(&state);
    if (key_state == KeychainBootstrapResult::kMissing) {
      family_lock_.Release();
      return;
    }
    if (key_state != KeychainBootstrapResult::kOk) {
      waiting_ = false;
      Finish("key_setup_keychain_recovery");
      return;
    }
    if (!state.canonical_key_present) {
      family_lock_.Release();
      return;
    }
    if (state.canonical_sha256 != claim_->key_sha256) {
      waiting_ = false;
      Finish("key_setup_key_mismatch");
      return;
    }
    pending_ = true;
    // Reinspect once on actual key arrival, not on every waiting cadence.
    VerifyAccount(false);
  }

  void CompleteVerifiedKey() {
    if (!authorization_.Run() || !claim_) {
      Finish("key_setup_cancelled");
      return;
    }
    KeychainBootstrapState state;
    if (keys_.ReadState(&state) != KeychainBootstrapResult::kOk) {
      Finish("key_setup_keychain_recovery");
      return;
    }
    if (state.canonical_sha256 != claim_->key_sha256) {
      if (state.candidate_sha256 != claim_->key_sha256 ||
          state.accepted_change_tag != claim_->change_tag ||
          keys_.PromoteAcceptedCandidate(claim_->change_tag) !=
              KeychainBootstrapResult::kOk) {
        Finish("key_setup_key_mismatch");
        return;
      }
    }
    auto verified_configuration = configuration_;
    verified_configuration.verified_key_sha256 = claim_->key_sha256;
    auto cryptor = LoadKeychainSyncPayloadCryptor(verified_configuration);
    if (!cryptor || !authorization_.Run()) {
      Finish("key_setup_keychain_recovery");
      return;
    }
    pending_ = false;
    waiting_ = false;
    family_lock_.Release();
    const auto callback = completion_;
    callback.Run({.configuration = configuration_,
                  .cryptor = std::move(cryptor),
                  .key_sha256 = claim_->key_sha256,
                  .authorization = authorization_});
  }

  void Finish(std::string issue, bool keep_waiting = false) {
    if (terminal_failure_) {
      return;
    }
    terminal_failure_ = !keep_waiting;
    pending_ = false;
    waiting_ = keep_waiting;
    [operation_ cancel];
    operation_ = nil;
    family_lock_.Release();
    if (!waiting_) {
      cancelled_->store(true, std::memory_order_release);
    }
    const auto callback = completion_;
    callback.Run({.issue = std::move(issue)});
  }

  const CloudKitSyncConfigurationMac configuration_;
  const SyncAuthorization original_;
  const Completion completion_;
  const scoped_refptr<base::SequencedTaskRunner> runner_;
  const std::shared_ptr<std::atomic<bool>> cancelled_ =
      std::make_shared<std::atomic<bool>>(false);
  const SyncAuthorization authorization_;
  KeychainSyncBootstrapMac keys_;
  KeyFamilyLock family_lock_;
  CKContainer* __strong container_ = nil;
  CKRecordZoneID* __strong zone_ = nil;
  CKRecordID* __strong identity_ = nil;
  CKOperation* __strong operation_ = nil;
  id __strong account_observer_ = nil;
  std::optional<Claim> claim_;
  bool created_zone_ = false;
  bool waiting_ = false;
  bool pending_ = true;
  bool terminal_failure_ = false;
};

std::unique_ptr<CloudKitSyncKeyBootstrapMac> CloudKitSyncKeyBootstrapMac::Start(
    const CloudKitSyncConfigurationMac& configuration,
    SyncAuthorization authorization,
    Completion completion) {
  auto core = std::make_shared<Core>(configuration, std::move(authorization),
                                     std::move(completion));
  core->Start();
  return std::unique_ptr<CloudKitSyncKeyBootstrapMac>(
      new CloudKitSyncKeyBootstrapMac(std::move(core)));
}
CloudKitSyncKeyBootstrapMac::CloudKitSyncKeyBootstrapMac(
    std::shared_ptr<Core> core)
    : core_(std::move(core)) {}
CloudKitSyncKeyBootstrapMac::~CloudKitSyncKeyBootstrapMac() {
  core_->Stop();
}
void CloudKitSyncKeyBootstrapMac::CheckWaitingKey() {
  core_->CheckWaitingKey();
}
bool CloudKitSyncKeyBootstrapMac::pending() const {
  return core_->pending();
}

}  // namespace ahoi::sync
