// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#import <CloudKit/CloudKit.h>
#import <Foundation/Foundation.h>

#include <optional>
#include <string>

#include "ahoi/browser/sync/cloudkit_sync_subscription_mac.h"
#include "testing/gtest/include/gtest/gtest.h"

// Stand-ins that archive under CloudKit's class names with the same keys as
// a real CKSyncEngineState / CKSyncEngineStateSerialization.
@interface AhoiTestEngineState : NSObject <NSSecureCoding>
@property(nonatomic, copy) NSString* existing;
@property(nonatomic, copy) NSString* token;
@property(nonatomic, copy) NSString* echo;
@property(nonatomic) BOOL needsSave;
@end

@implementation AhoiTestEngineState
@synthesize existing = _existing;
@synthesize token = _token;
@synthesize echo = _echo;
@synthesize needsSave = _needsSave;
+ (BOOL)supportsSecureCoding {
  return YES;
}
- (instancetype)initWithCoder:(NSCoder*)coder {
  if ((self = [super init])) {
    _existing = [coder decodeObjectOfClass:[NSString class]
                                    forKey:@"existingDatabaseSubscriptionID"];
    _token = [coder decodeObjectOfClass:[NSString class]
                                 forKey:@"serverChangeTokenForDatabase"];
    _echo = [coder decodeObjectOfClass:[NSString class] forKey:@"echo"];
    _needsSave = [coder decodeBoolForKey:@"needsToSaveDatabaseSubscription"];
  }
  return self;
}
- (void)encodeWithCoder:(NSCoder*)coder {
  [coder encodeObject:_existing forKey:@"existingDatabaseSubscriptionID"];
  [coder encodeObject:_token forKey:@"serverChangeTokenForDatabase"];
  [coder encodeObject:_echo forKey:@"echo"];
  [coder encodeBool:_needsSave forKey:@"needsToSaveDatabaseSubscription"];
}
@end

@interface AhoiTestStateSerialization : NSObject <NSSecureCoding>
@property(nonatomic, copy) NSData* data;
@end

@implementation AhoiTestStateSerialization
@synthesize data = _data;
+ (BOOL)supportsSecureCoding {
  return YES;
}
- (instancetype)initWithCoder:(NSCoder*)coder {
  if ((self = [super init])) {
    _data = [coder decodeObjectOfClass:[NSData class] forKey:@"data"];
  }
  return self;
}
- (void)encodeWithCoder:(NSCoder*)coder {
  [coder encodeObject:_data forKey:@"data"];
}
@end

namespace ahoi::sync {
namespace {

constexpr char kForeign[] =
    "app.ahoibrowser.AhoiBrowser.cloudkit-e2e.7e6bb1c73e544812ad24e816b486bc25";
constexpr char kConfigured[] =
    "AhoiSyncAcceptanceSubscription-23855a90-ee61-499e-abed-bfdc52a881d7";

NSData* Archive(id object, NSString* class_name) {
  NSKeyedArchiver* archiver =
      [[NSKeyedArchiver alloc] initRequiringSecureCoding:YES];
  [archiver setClassName:class_name forClass:[object class]];
  [archiver encodeObject:object forKey:NSKeyedArchiveRootObjectKey];
  [archiver finishEncoding];
  return archiver.encodedData;
}

NSData* EngineState(NSString* existing,
                    BOOL needs_save = NO,
                    NSString* echo = nil) {
  AhoiTestEngineState* state = [[AhoiTestEngineState alloc] init];
  state.existing = existing;
  state.token = @"database-token-0001";
  state.echo = echo;
  state.needsSave = needs_save;
  AhoiTestStateSerialization* serialization =
      [[AhoiTestStateSerialization alloc] init];
  serialization.data = Archive(state, @"CKSyncEngineState");
  return Archive(serialization, @"CKSyncEngineStateSerialization");
}

AhoiTestEngineState* DecodeState(NSData* archived) {
  NSKeyedUnarchiver* outer =
      [[NSKeyedUnarchiver alloc] initForReadingFromData:archived error:nil];
  [outer setClass:[AhoiTestStateSerialization class]
      forClassName:@"CKSyncEngineStateSerialization"];
  AhoiTestStateSerialization* serialization =
      [outer decodeObjectOfClass:[AhoiTestStateSerialization class]
                          forKey:NSKeyedArchiveRootObjectKey];
  NSKeyedUnarchiver* inner =
      [[NSKeyedUnarchiver alloc] initForReadingFromData:serialization.data
                                                  error:nil];
  [inner setClass:[AhoiTestEngineState class]
      forClassName:@"CKSyncEngineState"];
  return [inner decodeObjectOfClass:[AhoiTestEngineState class]
                             forKey:NSKeyedArchiveRootObjectKey];
}

NSString* NS(const char* value) {
  return [NSString stringWithUTF8String:value];
}

}  // namespace

TEST(CloudKitSyncSubscriptionTest, ReadsTheAdoptedSubscription) {
  const auto state = ReadEngineSubscription(EngineState(NS(kForeign)));
  ASSERT_TRUE(state);
  EXPECT_EQ(state->remembered, kForeign);
  EXPECT_FALSE(state->needs_save);
  EXPECT_TRUE(ShouldRebindEngineSubscription(*state, kConfigured));
}

TEST(CloudKitSyncSubscriptionTest, RebindsToConfiguredAndKeepsEverythingElse) {
  NSData* original = EngineState(NS(kForeign));
  NSData* rebound = RebindEngineSubscription(original, kConfigured);
  ASSERT_TRUE(rebound);

  const auto state = ReadEngineSubscription(rebound);
  ASSERT_TRUE(state);
  EXPECT_EQ(state->remembered, kConfigured);
  EXPECT_TRUE(state->needs_save);
  EXPECT_FALSE(ShouldRebindEngineSubscription(*state, kConfigured));
  // A second load is a no-op, so the migration runs once.
  EXPECT_FALSE(RebindEngineSubscription(rebound, kConfigured));

  AhoiTestEngineState* decoded = DecodeState(rebound);
  EXPECT_TRUE([decoded.existing isEqualToString:NS(kConfigured)]);
  EXPECT_TRUE([decoded.token isEqualToString:@"database-token-0001"]);
  EXPECT_TRUE(decoded.needsSave);
  if (@available(macOS 14.0, *)) {
    NSError* error = nil;
    EXPECT_TRUE([NSKeyedUnarchiver
        unarchivedObjectOfClass:[CKSyncEngineStateSerialization class]
                       fromData:rebound
                          error:&error]);
    EXPECT_FALSE(error);
  }
}

TEST(CloudKitSyncSubscriptionTest, LeavesMatchingOrUnsetStateAlone) {
  EXPECT_FALSE(RebindEngineSubscription(EngineState(NS(kConfigured)),
                                        kConfigured));
  // Nothing adopted yet: the engine saves the configured ID by itself.
  const auto fresh = ReadEngineSubscription(EngineState(nil, YES));
  ASSERT_TRUE(fresh);
  EXPECT_TRUE(fresh->remembered.empty());
  EXPECT_TRUE(fresh->needs_save);
  EXPECT_FALSE(RebindEngineSubscription(EngineState(nil, YES), kConfigured));
  // No configured ID: the engine's own choice stands.
  EXPECT_FALSE(RebindEngineSubscription(EngineState(NS(kForeign)), ""));
}

TEST(CloudKitSyncSubscriptionTest, RefusesAmbiguousOrUnknownArchives) {
  // A second, separately archived copy of the old ID might mean something
  // else; the state is left untouched.
  NSString* echo = [NSString stringWithFormat:@"%s", kForeign];
  NSData* ambiguous =
      EngineState([NSString stringWithFormat:@"%s", kForeign], NO, echo);
  ASSERT_TRUE(ReadEngineSubscription(ambiguous));
  EXPECT_FALSE(RebindEngineSubscription(ambiguous, kConfigured));
  EXPECT_FALSE(ReadEngineSubscription(nil));
  EXPECT_FALSE(ReadEngineSubscription([@"not a plist" dataUsingEncoding:
                                           NSUTF8StringEncoding]));
  AhoiTestStateSerialization* other = [[AhoiTestStateSerialization alloc] init];
  other.data = [NSKeyedArchiver archivedDataWithRootObject:@[ NS(kForeign) ]
                                      requiringSecureCoding:YES
                                                      error:nil];
  EXPECT_FALSE(ReadEngineSubscription(
      Archive(other, @"CKSyncEngineStateSerialization")));
  EXPECT_FALSE(RebindEngineSubscription(
      Archive(other, @"CKSyncEngineStateSerialization"), kConfigured));
}

}  // namespace ahoi::sync
