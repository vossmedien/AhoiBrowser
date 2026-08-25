// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#import "ahoi/browser/updater/sparkle_runtime_mac.h"

#import <Sparkle/Sparkle.h>

#include <algorithm>
#include <optional>
#include <string>

#include "ahoi/browser/updater/update_channel.h"
#include "ahoi/browser/updater/update_configuration.h"
#include "ahoi/browser/updater/update_status.h"
#include "ahoi/browser/updater/update_strings.h"
#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"

namespace {

using ahoi::updater::ConfigurationErrorName;
using ahoi::updater::ConfigurationInput;
using ahoi::updater::LocalizedUpdateString;
using ahoi::updater::UpdateChannelName;
using ahoi::updater::UpdateConfiguration;
using ahoi::updater::UpdateStage;
using ahoi::updater::UpdateStatusModel;
using ahoi::updater::UpdateString;
using ahoi::updater::ValidateConfiguration;

NSString* Local(UpdateString key) {
  NSString* locale = NSLocale.currentLocale.localeIdentifier ?: @"en";
  return base::SysUTF8ToNSString(
      LocalizedUpdateString(key, base::SysNSStringToUTF8(locale)));
}

NSString* PlistString(NSDictionary* info, NSString* key) {
  id value = info[key];
  return [value isKindOfClass:NSString.class] ? value : @"";
}

bool PlistBool(NSDictionary* info, NSString* key) {
  id value = info[key];
  return [value isKindOfClass:NSNumber.class] && [value boolValue];
}

UpdateString StringForStage(UpdateStage stage) {
  switch (stage) {
    case UpdateStage::kUnavailable:
      return UpdateString::kUnavailable;
    case UpdateStage::kIdle:
      return UpdateString::kIdle;
    case UpdateStage::kChecking:
      return UpdateString::kChecking;
    case UpdateStage::kUpdateAvailable:
      return UpdateString::kUpdateAvailable;
    case UpdateStage::kDownloading:
      return UpdateString::kDownloading;
    case UpdateStage::kDownloaded:
      return UpdateString::kDownloaded;
    case UpdateStage::kInstalling:
      return UpdateString::kInstalling;
    case UpdateStage::kRelaunching:
      return UpdateString::kRelaunching;
    case UpdateStage::kUpToDate:
      return UpdateString::kUpToDate;
    case UpdateStage::kError:
      return UpdateString::kError;
  }
  return UpdateString::kError;
}

NSString* const kAhoiUpdateCheckMenuMarker = @"AhoiUpdateCheckMenu";
NSString* const kAhoiUpdateSettingsMenuMarker = @"AhoiUpdateSettingsMenu";

}  // namespace

@interface AhoiSparkleRuntime () <SPUUpdaterDelegate>
@end

@implementation AhoiSparkleRuntime {
  SPUStandardUpdaterController* __strong _controller;
  std::optional<UpdateConfiguration> _configuration;
  UpdateStatusModel _status;
  BOOL _started;
}

+ (AhoiSparkleRuntime*)sharedRuntime {
  static AhoiSparkleRuntime* runtime = [[AhoiSparkleRuntime alloc] initPrivate];
  return runtime;
}

- (instancetype)initPrivate {
  return [super init];
}

- (void)installMainMenuItems {
  NSMenu* applicationMenu = [NSApp.mainMenu itemAtIndex:0].submenu;
  if (!applicationMenu) {
    return;
  }
  for (NSMenuItem* item in applicationMenu.itemArray) {
    if ([item.representedObject isEqual:kAhoiUpdateCheckMenuMarker]) {
      return;
    }
  }

  NSInteger insertionIndex =
      std::min<NSInteger>(2, applicationMenu.numberOfItems);
  NSMenuItem* checkItem =
      [[NSMenuItem alloc] initWithTitle:Local(UpdateString::kCheckMenu)
                                 action:@selector(checkForUpdates:)
                          keyEquivalent:@""];
  checkItem.target = self;
  checkItem.representedObject = kAhoiUpdateCheckMenuMarker;
  checkItem.accessibilityLabel = Local(UpdateString::kCheckMenu);
  [applicationMenu insertItem:checkItem atIndex:insertionIndex++];

  NSMenuItem* settingsItem =
      [[NSMenuItem alloc] initWithTitle:Local(UpdateString::kSettingsMenu)
                                 action:@selector(showUpdateSettings:)
                          keyEquivalent:@""];
  settingsItem.target = self;
  settingsItem.representedObject = kAhoiUpdateSettingsMenuMarker;
  settingsItem.accessibilityLabel = Local(UpdateString::kSettingsMenu);
  [applicationMenu insertItem:settingsItem atIndex:insertionIndex];
  [applicationMenu insertItem:NSMenuItem.separatorItem
                      atIndex:insertionIndex + 1];
}

- (void)start {
  if (_started) {
    return;
  }
  _started = YES;

  NSDictionary* info = NSBundle.mainBundle.infoDictionary;
  NSBundle* sparkleBundle =
      [NSBundle bundleForClass:SPUStandardUpdaterController.class];
  ConfigurationInput input = {
      .channel =
          base::SysNSStringToUTF8(PlistString(info, @"AhoiUpdateChannel")),
      .feed_url = base::SysNSStringToUTF8(PlistString(info, @"SUFeedURL")),
      .public_ed_key =
          base::SysNSStringToUTF8(PlistString(info, @"SUPublicEDKey")),
      .framework_version = base::SysNSStringToUTF8(PlistString(
          sparkleBundle.infoDictionary, @"CFBundleShortVersionString")),
      .require_signed_feed = PlistBool(info, @"SURequireSignedFeed"),
      .verify_before_extraction =
          PlistBool(info, @"SUVerifyUpdateBeforeExtraction"),
      .sends_system_profile = PlistBool(info, @"SUSendProfileInfo"),
  };
  auto result = ValidateConfiguration(input);
  if (!result.ok()) {
    const std::string reason =
        std::string(ConfigurationErrorName(*result.error));
    _status.SetUnavailable(reason);
    LOG(ERROR) << "Ahoi updater disabled (fail closed): " << reason;
    return;
  }
  _configuration = std::move(result.configuration);
  _controller =
      [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
                                                    updaterDelegate:self
                                                 userDriverDelegate:nil];
  NSError* startError = nil;
  if (![_controller.updater startUpdater:&startError]) {
    const std::string reason = base::SysNSStringToUTF8(
        startError.localizedDescription ?: @"Sparkle failed to start");
    _status.SetUnavailable(reason);
    _controller = nil;
    LOG(ERROR) << "Ahoi updater disabled (Sparkle start failed): " << reason;
    return;
  }
  _status.SetReady();
}

- (IBAction)checkForUpdates:(id)sender {
  if (!_configuration || !_controller.updater.canCheckForUpdates ||
      !_status.BeginCheck()) {
    NSBeep();
    return;
  }
  [self announceCurrentStatus];
  [_controller checkForUpdates:sender];
}

- (IBAction)showUpdateSettings:(id)sender {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = Local(UpdateString::kSettingsTitle);
  alert.informativeText = Local(UpdateString::kSecurityHelp);
  [alert addButtonWithTitle:Local(UpdateString::kCheckNow)];
  [alert addButtonWithTitle:Local(UpdateString::kDone)];

  NSStackView* stack = [NSStackView stackViewWithViews:@[]];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 10;
  stack.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);

  NSTextField* statusLabel = [NSTextField labelWithString:[self statusText]];
  statusLabel.accessibilityLabel = Local(UpdateString::kStatusLabel);
  statusLabel.maximumNumberOfLines = 3;
  statusLabel.lineBreakMode = NSLineBreakByWordWrapping;
  [stack addArrangedSubview:statusLabel];

  NSString* channelName =
      _configuration
          ? base::SysUTF8ToNSString(UpdateChannelName(_configuration->channel))
          : PlistString(NSBundle.mainBundle.infoDictionary,
                        @"AhoiUpdateChannel");
  NSPopUpButton* channel = [[NSPopUpButton alloc] initWithFrame:NSZeroRect];
  [channel addItemsWithTitles:@[ @"stable", @"beta", @"nightly" ]];
  [channel selectItemWithTitle:channelName];
  channel.enabled = NO;
  channel.accessibilityLabel = Local(UpdateString::kChannelLabel);
  [stack addArrangedSubview:channel];

  NSButton* automaticChecks =
      [NSButton checkboxWithTitle:Local(UpdateString::kAutomaticChecks)
                           target:nil
                           action:nil];
  automaticChecks.state = _controller.updater.automaticallyChecksForUpdates
                              ? NSControlStateValueOn
                              : NSControlStateValueOff;
  automaticChecks.enabled = _configuration.has_value();
  [stack addArrangedSubview:automaticChecks];

  NSButton* automaticDownloads =
      [NSButton checkboxWithTitle:Local(UpdateString::kAutomaticDownloads)
                           target:nil
                           action:nil];
  automaticDownloads.state = _controller.updater.automaticallyDownloadsUpdates
                                 ? NSControlStateValueOn
                                 : NSControlStateValueOff;
  automaticDownloads.enabled = _configuration.has_value();
  [stack addArrangedSubview:automaticDownloads];

  stack.frame = NSMakeRect(0, 0, 460, 145);
  alert.accessoryView = stack;
  alert.buttons.firstObject.enabled =
      _configuration && _controller.updater.canCheckForUpdates;

  NSModalResponse response = [alert runModal];
  if (_configuration) {
    _controller.updater.automaticallyChecksForUpdates =
        automaticChecks.state == NSControlStateValueOn;
    _controller.updater.automaticallyDownloadsUpdates =
        automaticDownloads.state == NSControlStateValueOn;
  }
  if (response == NSAlertFirstButtonReturn) {
    [self checkForUpdates:sender];
  }
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem {
  if (menuItem.action == @selector(checkForUpdates:)) {
    return _configuration && _controller.updater.canCheckForUpdates;
  }
  return YES;
}

- (NSString*)statusText {
  NSString* text = Local(StringForStage(_status.status().stage));
  if (!_status.status().version.empty()) {
    return [NSString
        stringWithFormat:@"%@ (%@)", text,
                         base::SysUTF8ToNSString(_status.status().version)];
  }
  return text;
}

- (void)announceCurrentStatus {
  NSAccessibilityPostNotificationWithUserInfo(
      NSApp, NSAccessibilityAnnouncementRequestedNotification, @{
        NSAccessibilityAnnouncementKey : [self statusText],
        NSAccessibilityPriorityKey : @(NSAccessibilityPriorityMedium)
      });
}

#pragma mark - SPUUpdaterDelegate

- (NSSet<NSString*>*)allowedChannelsForUpdater:(SPUUpdater*)updater {
  NSMutableSet<NSString*>* channels = [NSMutableSet set];
  if (_configuration) {
    for (std::string_view channel :
         ahoi::updater::AllowedSparkleChannels(_configuration->channel)) {
      [channels addObject:base::SysUTF8ToNSString(channel)];
    }
  }
  return channels;
}

- (BOOL)updater:(SPUUpdater*)updater
    mayPerformUpdateCheck:(SPUUpdateCheck)updateCheck
                    error:(NSError* __autoreleasing*)error {
  if (_configuration) {
    if (_status.status().stage == UpdateStage::kIdle ||
        _status.status().stage == UpdateStage::kUpToDate ||
        _status.status().stage == UpdateStage::kError) {
      _status.BeginCheck();
    }
    return YES;
  }
  if (error) {
    *error = [NSError
        errorWithDomain:@"app.ahoibrowser.updater"
                   code:1
               userInfo:@{
                 NSLocalizedDescriptionKey : Local(UpdateString::kUnavailable)
               }];
  }
  return NO;
}

- (NSArray<NSDictionary<NSString*, NSString*>*>*)
    feedParametersForUpdater:(SPUUpdater*)updater
        sendingSystemProfile:(BOOL)sendingProfile {
  return @[];
}

- (BOOL)updaterShouldPromptForPermissionToCheckForUpdates:(SPUUpdater*)updater {
  return NO;
}

- (NSArray<NSString*>*)allowedSystemProfileKeysForUpdater:(SPUUpdater*)updater {
  return @[];
}

- (void)updater:(SPUUpdater*)updater didFindValidUpdate:(SUAppcastItem*)item {
  _status.FoundUpdate(base::SysNSStringToUTF8(item.displayVersionString));
  [self announceCurrentStatus];
}

- (void)updaterDidNotFindUpdate:(SPUUpdater*)updater error:(NSError*)error {
  _status.MarkUpToDate();
  [self announceCurrentStatus];
}

- (void)updater:(SPUUpdater*)updater
    willDownloadUpdate:(SUAppcastItem*)item
           withRequest:(NSMutableURLRequest*)request {
  _status.BeginDownload();
  [self announceCurrentStatus];
}

- (void)updater:(SPUUpdater*)updater didDownloadUpdate:(SUAppcastItem*)item {
  _status.FinishDownload();
  [self announceCurrentStatus];
}

- (void)updater:(SPUUpdater*)updater
    failedToDownloadUpdate:(SUAppcastItem*)item
                     error:(NSError*)error {
  _status.Fail(base::SysNSStringToUTF8(error.localizedDescription));
  [self announceCurrentStatus];
}

- (void)updater:(SPUUpdater*)updater willInstallUpdate:(SUAppcastItem*)item {
  if (_status.status().stage == UpdateStage::kUpdateAvailable) {
    _status.BeginInstall();
  } else if (_status.status().stage == UpdateStage::kDownloaded) {
    _status.BeginInstall();
  }
  [self announceCurrentStatus];
}

- (void)updaterWillRelaunchApplication:(SPUUpdater*)updater {
  _status.BeginRelaunch();
  [self announceCurrentStatus];
}

- (void)updater:(SPUUpdater*)updater didAbortWithError:(NSError*)error {
  _status.Fail(base::SysNSStringToUTF8(error.localizedDescription));
  [self announceCurrentStatus];
}

@end
