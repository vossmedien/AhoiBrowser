// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UPDATER_SPARKLE_RUNTIME_MAC_H_
#define AHOI_BROWSER_UPDATER_SPARKLE_RUNTIME_MAC_H_

#import <Cocoa/Cocoa.h>

// Application-wide native adapter around the pinned upstream Sparkle
// framework. It owns presentation/status only; download, extraction,
// installation, atomic replacement and relaunch remain Sparkle
// responsibilities.
@interface AhoiSparkleRuntime : NSObject <NSMenuItemValidation>

@property(readonly, nonatomic, class) AhoiSparkleRuntime* sharedRuntime;

- (void)installMainMenuItems;
- (void)start;
- (IBAction)checkForUpdates:(id)sender;
- (IBAction)showUpdateSettings:(id)sender;

@end

#endif  // AHOI_BROWSER_UPDATER_SPARKLE_RUNTIME_MAC_H_
