// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/cloudkit_sync_subscription_mac.h"

namespace {

NSString* const kEngineStateClass = @"CKSyncEngineState";
NSString* const kExistingKey = @"existingDatabaseSubscriptionID";
NSString* const kNeedsSaveKey = @"needsToSaveDatabaseSubscription";

}  // namespace

// Decodes only the two subscription fields of CKSyncEngineState. Keyed
// decoding ignores every other key, so no private CloudKit class is built.
@interface AhoiEngineSubscriptionProbe : NSObject <NSSecureCoding>
@property(nonatomic, copy) NSString* remembered;
@property(nonatomic) BOOL needsSave;
@property(nonatomic) BOOL recognized;
@end

@implementation AhoiEngineSubscriptionProbe

@synthesize remembered = _remembered;
@synthesize needsSave = _needsSave;
@synthesize recognized = _recognized;

+ (BOOL)supportsSecureCoding {
  return YES;
}

- (instancetype)initWithCoder:(NSCoder*)coder {
  if ((self = [super init])) {
    _recognized = [coder containsValueForKey:kNeedsSaveKey];
    _remembered = [coder decodeObjectOfClass:[NSString class]
                                      forKey:kExistingKey];
    _needsSave = [coder decodeBoolForKey:kNeedsSaveKey];
    if (coder.error) {
      _recognized = NO;
    }
  }
  return self;
}

- (void)encodeWithCoder:(NSCoder*)coder {
}

@end

namespace ahoi::sync {
namespace {

NSMutableDictionary* ParseArchive(NSData* data) {
  if (!data) {
    return nil;
  }
  NSError* error = nil;
  id plist = [NSPropertyListSerialization
      propertyListWithData:data
                   options:NSPropertyListMutableContainers
                    format:nil
                     error:&error];
  if (error || ![plist isKindOfClass:[NSMutableDictionary class]]) {
    return nil;
  }
  NSMutableDictionary* archive = plist;
  if (![archive[@"$archiver"] isEqual:@"NSKeyedArchiver"] ||
      ![archive[@"$objects"] isKindOfClass:[NSMutableArray class]]) {
    return nil;
  }
  return archive;
}

NSData* WriteArchive(NSDictionary* archive) {
  return [NSPropertyListSerialization
      dataWithPropertyList:archive
                    format:NSPropertyListBinaryFormat_v1_0
                   options:0
                     error:nil];
}

// Index of the only $objects entry that satisfies `match`, or NSNotFound.
NSUInteger OnlyObject(NSArray* objects, BOOL (^match)(id)) {
  NSIndexSet* found = [objects
      indexesOfObjectsPassingTest:^BOOL(id object, NSUInteger, BOOL*) {
        return match(object);
      }];
  return found.count == 1 ? found.firstIndex : NSNotFound;
}

// The outer CKSyncEngineStateSerialization archive holds exactly one data
// blob: the inner CKSyncEngineState archive.
NSUInteger InnerStateIndex(NSArray* outer_objects) {
  return OnlyObject(outer_objects,
                    ^BOOL(id object) {
                      return [object isKindOfClass:[NSData class]];
                    });
}

std::optional<EngineSubscriptionState> ProbeInnerState(NSData* inner) {
  NSError* error = nil;
  NSKeyedUnarchiver* unarchiver =
      [[NSKeyedUnarchiver alloc] initForReadingFromData:inner error:&error];
  if (!unarchiver || error) {
    return std::nullopt;
  }
  unarchiver.requiresSecureCoding = YES;
  unarchiver.decodingFailurePolicy = NSDecodingFailurePolicySetErrorAndReturn;
  [unarchiver setClass:[AhoiEngineSubscriptionProbe class]
          forClassName:kEngineStateClass];
  AhoiEngineSubscriptionProbe* probe =
      [unarchiver decodeObjectOfClass:[AhoiEngineSubscriptionProbe class]
                               forKey:NSKeyedArchiveRootObjectKey];
  [unarchiver finishDecoding];
  if (unarchiver.error || ![probe isKindOfClass:[AhoiEngineSubscriptionProbe
                                                   class]] ||
      !probe.recognized) {
    return std::nullopt;
  }
  return EngineSubscriptionState{
      .remembered = probe.remembered ? probe.remembered.UTF8String : "",
      .needs_save = static_cast<bool>(probe.needsSave)};
}

NSData* RebindInnerState(NSData* inner,
                         const std::string& old_id,
                         NSString* new_id) {
  NSMutableDictionary* archive = ParseArchive(inner);
  if (!archive) {
    return nil;
  }
  NSMutableArray* objects = archive[@"$objects"];
  NSString* old_string = [NSString stringWithUTF8String:old_id.c_str()];
  const NSUInteger root = OnlyObject(objects, ^BOOL(id object) {
    return [object isKindOfClass:[NSDictionary class]] &&
           [object objectForKey:kExistingKey] &&
           [[object objectForKey:kNeedsSaveKey]
               isKindOfClass:[NSNumber class]];
  });
  // Only one string may carry the old identifier, so replacing it cannot
  // change the meaning of any other field.
  const NSUInteger remembered = OnlyObject(objects, ^BOOL(id object) {
    return [object isKindOfClass:[NSString class]] &&
           [object isEqualToString:old_string];
  });
  if (root == NSNotFound || remembered == NSNotFound) {
    return nil;
  }
  objects[remembered] = [new_id copy];
  NSMutableDictionary* state = objects[root];
  state[kNeedsSaveKey] = @YES;
  return WriteArchive(archive);
}

}  // namespace

std::optional<EngineSubscriptionState> ReadEngineSubscription(
    NSData* archived) {
  NSMutableDictionary* outer = ParseArchive(archived);
  if (!outer) {
    return std::nullopt;
  }
  NSArray* objects = outer[@"$objects"];
  const NSUInteger inner = InnerStateIndex(objects);
  if (inner == NSNotFound) {
    return std::nullopt;
  }
  return ProbeInnerState(objects[inner]);
}

bool ShouldRebindEngineSubscription(const EngineSubscriptionState& state,
                                    const std::string& configured) {
  return !configured.empty() && !state.remembered.empty() &&
         state.remembered != configured;
}

NSData* RebindEngineSubscription(NSData* archived,
                                 const std::string& subscription_id) {
  const std::optional<EngineSubscriptionState> before =
      ReadEngineSubscription(archived);
  if (!before || !ShouldRebindEngineSubscription(*before, subscription_id)) {
    return nil;
  }
  NSMutableDictionary* outer = ParseArchive(archived);
  NSMutableArray* objects = outer[@"$objects"];
  const NSUInteger inner_index = InnerStateIndex(objects);
  NSString* new_id = [NSString stringWithUTF8String:subscription_id.c_str()];
  NSData* inner = RebindInnerState(objects[inner_index], before->remembered,
                                   new_id);
  if (!inner) {
    return nil;
  }
  objects[inner_index] = inner;
  NSData* result = WriteArchive(outer);
  const std::optional<EngineSubscriptionState> after =
      ReadEngineSubscription(result);
  if (!after || after->remembered != subscription_id || !after->needs_save) {
    return nil;
  }
  return result;
}

}  // namespace ahoi::sync
