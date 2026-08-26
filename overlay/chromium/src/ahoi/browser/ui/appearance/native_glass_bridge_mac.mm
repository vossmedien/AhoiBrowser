// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#import "ahoi/browser/ui/appearance/native_glass_bridge.h"

#import <AppKit/AppKit.h>

#include <optional>
#include <utility>

#import "skia/ext/skia_utils_mac.h"

// The material is a visual background. AppKit's default NSGlassEffectView hit
// testing would otherwise steal clicks from sibling Views and WebContents.
API_AVAILABLE(macos(26.0))
@interface AhoiChromeGlassBackgroundView : NSGlassEffectView
@end

@implementation AhoiChromeGlassBackgroundView
- (NSView*)hitTest:(NSPoint)point {
  return nil;
}
@end

@interface AhoiOpaqueChromeBackgroundView : NSView {
 @private
  NSColor* _fillColor;
}
@property(nonatomic, strong) NSColor* fillColor;
@end

@implementation AhoiOpaqueChromeBackgroundView
@synthesize fillColor = _fillColor;

- (BOOL)isOpaque {
  return YES;
}

- (NSView*)hitTest:(NSPoint)point {
  return nil;
}

- (void)drawRect:(NSRect)dirtyRect {
  [self.fillColor setFill];
  NSRectFill(dirtyRect);
}
@end

namespace {

NSGlassEffectViewStyle ToAppKitStyle(ahoi::appearance::NativeGlassStyle style)
    API_AVAILABLE(macos(26.0)) {
  switch (style) {
    case ahoi::appearance::NativeGlassStyle::kRegular:
      return NSGlassEffectViewStyleRegular;
    case ahoi::appearance::NativeGlassStyle::kClear:
      return NSGlassEffectViewStyleClear;
  }
  return NSGlassEffectViewStyleRegular;
}

}  // namespace

namespace ahoi::appearance {

class NativeChromeMaterialBridge::Impl final {
 public:
  explicit Impl(gfx::NativeWindow window) : window_(std::move(window)) {}

  ~Impl() { Reset(); }

  void Apply(const NativeChromeMaterialConfiguration& configuration) {
    NativeChromeMaterialConfiguration effective = configuration;
    effective.use_native_glass =
        configuration.use_native_glass && IsNativeMacGlassAvailable();

    NSWindow* window = window_.GetNativeNSWindow();
    if (!window) {
      Reset();
      return;
    }
    NSView* content_view = window.contentView;
    if (!content_view) {
      Reset();
      return;
    }
    if (configuration_ == effective &&
        material_view_.superview == content_view) {
      return;
    }

    RemoveMaterialView();
    configuration_ = effective;

    if (effective.use_native_glass) {
      if (@available(macOS 26.0, *)) {
        auto* glass_view = [[AhoiChromeGlassBackgroundView alloc]
            initWithFrame:content_view.bounds];
        glass_view.style = ToAppKitStyle(effective.style);
        glass_view.cornerRadius = effective.corner_radius;
        glass_view.tintColor = skia::SkColorToSRGBNSColor(effective.tint_color);
        material_view_ = glass_view;
      }
    }

    if (!material_view_) {
      auto* opaque_view = [[AhoiOpaqueChromeBackgroundView alloc]
          initWithFrame:content_view.bounds];
      opaque_view.fillColor =
          skia::SkColorToSRGBNSColor(effective.fallback_color);
      material_view_ = opaque_view;
      effective.use_native_glass = false;
      configuration_ = effective;
    }

    material_view_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    material_view_.accessibilityElement = NO;
    [content_view addSubview:material_view_
                  positioned:NSWindowBelow
                  relativeTo:nil];

    if (effective.use_native_glass) {
      window.opaque = NO;
      // A fully clear NSWindow continuously invalidates its surface. This tiny
      // alpha preserves native glass while avoiding that AppKit energy cost.
      window.backgroundColor =
          [NSColor.windowBackgroundColor colorWithAlphaComponent:0.001];
    } else {
      window.backgroundColor =
          skia::SkColorToSRGBNSColor(effective.fallback_color);
      window.opaque = YES;
    }
  }

  void Reset() {
    RemoveMaterialView();
    configuration_.reset();
  }

  bool is_using_native_glass() const {
    return configuration_.has_value() && configuration_->use_native_glass;
  }

 private:
  void RemoveMaterialView() {
    [material_view_ removeFromSuperview];
    material_view_ = nil;
  }

  gfx::NativeWindow window_;
  NSView* __strong material_view_;
  std::optional<NativeChromeMaterialConfiguration> configuration_;
};

NativeChromeMaterialBridge::NativeChromeMaterialBridge(gfx::NativeWindow window)
    : impl_(std::make_unique<Impl>(std::move(window))) {}

NativeChromeMaterialBridge::~NativeChromeMaterialBridge() = default;

void NativeChromeMaterialBridge::Apply(
    const NativeChromeMaterialConfiguration& configuration) {
  impl_->Apply(configuration);
}

void NativeChromeMaterialBridge::Reset() {
  impl_->Reset();
}

bool NativeChromeMaterialBridge::is_using_native_glass_for_testing() const {
  return impl_->is_using_native_glass();
}

bool IsNativeMacGlassAvailable() {
  if (@available(macOS 26.0, *)) {
    return NSClassFromString(@"NSGlassEffectView") != nil;
  }
  return false;
}

}  // namespace ahoi::appearance
