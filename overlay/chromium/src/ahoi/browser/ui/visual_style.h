// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_VISUAL_STYLE_H_
#define AHOI_BROWSER_UI_VISUAL_STYLE_H_

#include "base/time/time.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"

namespace ahoi::visual_style {

// A small semantic layer keeps Ahoi chrome visually coherent without freezing
// it to one hard-coded palette. These IDs deliberately point at Chromium's
// semantic ColorProvider roles instead of concrete RGB values. ColorProvider
// resolves them for light, dark, high-contrast and Chrome-theme contexts. A
// future Ahoi theme mixer can override the same roles centrally without
// changing any component implementation.
inline constexpr ui::ColorId kChromeSurface = ui::kColorSysSurface2;
inline constexpr ui::ColorId kRaisedSurface = ui::kColorSysSurface3;
inline constexpr ui::ColorId kSelectedSurface = ui::kColorSysSurface4;
inline constexpr ui::ColorId kAccent = ui::kColorSysPrimary;
inline constexpr ui::ColorId kText = ui::kColorSysOnSurface;
inline constexpr ui::ColorId kMutedText = ui::kColorSysOnSurfaceSubtle;
inline constexpr ui::ColorId kDivider = ui::kColorSysDivider;
inline constexpr ui::ColorId kHoverSurface = ui::kColorSysStateHoverOnSubtle;
inline constexpr ui::ColorId kDropTargetSurface = ui::kColorSysTonalContainer;
inline constexpr ui::ColorId kFocusRing = ui::kColorSysStateFocusRing;
inline constexpr ui::ColorId kDisabledIcon = ui::kColorIconDisabled;
inline constexpr ui::ColorId kModalScrim = ui::kColorSysStateScrim;

// Chromium ThemeService's user color is the one global chrome accent input.
// This default belongs only to persisted workspace/folder identity data and
// must never be used as a component paint replacement for `kAccent` above.
inline constexpr SkColor kDefaultAccent = SkColorSetRGB(0x0A, 0x84, 0xFF);
// Persisted workspace/folder accent choices. These are user data values, not
// component paint colors; Views still resolve every chrome state through the
// semantic ColorProvider roles above.
inline constexpr SkColor kUserAccentRed = SkColorSetRGB(0xE4, 0x5E, 0x68);
inline constexpr SkColor kUserAccentOrange = SkColorSetRGB(0xF2, 0x8C, 0x45);
inline constexpr SkColor kUserAccentYellow = SkColorSetRGB(0xE2, 0xB8, 0x4B);
inline constexpr SkColor kUserAccentGreen = SkColorSetRGB(0x54, 0xA9, 0x6B);
inline constexpr SkColor kUserAccentBlue = SkColorSetRGB(0x4F, 0x8D, 0xE8);
inline constexpr SkColor kUserAccentViolet = SkColorSetRGB(0x8B, 0x6A, 0xDD);

// Shared geometry tokens. Keep dimensions here rather than in individual
// views so later density/theme work can tune the complete native surface as a
// unit. The command bar's content width is derived from its outer width and
// delegate margins; it must not be re-encoded in the view.
inline constexpr int kSidebarWidthDefault = 264;
inline constexpr int kSidebarWidthMinimum = 208;
inline constexpr int kSidebarWidthMaximum = 420;
inline constexpr int kSidebarResizeAreaWidth = 10;
// The trigger remains transparent and is present only while hidden. It is
// deliberately narrower than the native resize affordance so ordinary page
// interaction near the left edge is not captured accidentally.
inline constexpr int kSidebarEdgeRevealHotZoneWidth = 5;
// Compatibility alias for components that use the default layout width.
inline constexpr int kSidebarWidth = kSidebarWidthDefault;
inline constexpr int kSidebarHorizontalInset = 10;
inline constexpr int kSidebarTopInset = 10;
inline constexpr int kSidebarBottomInset = 8;
// Floating/edge-revealed presentation is a card above the page rather than a
// flush dock. Its leading, top and bottom breathing room form one continuous
// outer gutter; the narrower trailing gap keeps the resize affordance close to
// the WebContents without making the card look glued to the window edge.
inline constexpr int kFloatingSidebarOuterInset = 14;
inline constexpr int kFloatingSidebarLeadingInset = kFloatingSidebarOuterInset;
inline constexpr int kFloatingSidebarTrailingInset = 8;
inline constexpr int kFloatingSidebarCornerRadius = 14;
inline constexpr int kSidebarPresentationRevealOffset = 18;
// Fixed window-drag/caption area above the sidebar. Floating browser chrome
// must never change this value or shift the complete sidebar surface.
inline constexpr int kSidebarTitlebarHeight = 40;
inline constexpr int kSidebarSectionSpacing = 6;
inline constexpr int kSidebarContentWidth =
    kSidebarWidthDefault - (2 * kSidebarHorizontalInset);
// Saved, temporary, remote and split-tab rows share one density contract.
// Keeping the token sidebar-semantic rather than tree-specific prevents the
// runtime and remote sections from drifting to separate per-view heights.
inline constexpr int kSidebarTabRowHeight = 40;
inline constexpr int kTreeIndent = 16;
// Every saved, temporary and split-pane row keeps the same breathing room
// around its active/hover surface. Split segments use the narrower horizontal
// inset so the miniature grid remains readable at sidebar width.
inline constexpr int kSidebarTabRowVerticalInset = 2;
inline constexpr int kSidebarTabRowHorizontalInset = 4;
inline constexpr int kSidebarSplitPaneHorizontalInset = 1;
inline constexpr int kSidebarSplitPaneGap = 2;
// Stacked and grid split segments remain compact, but must still leave enough
// room for a favicon, an elided title and a reliable desktop pointer target.
inline constexpr int kSidebarSplitPaneMinimumHeight = 30;
// Native drag targets use one inset and two deliberately restrained emphasis
// levels. The idle outline makes the complete drop surface discoverable;
// acceptance strengthens it without changing geometry under the pointer.
inline constexpr int kSidebarDropTargetInset = kSidebarTabRowHorizontalInset;
inline constexpr int kSidebarDropTargetOutlineThickness = 1;
inline constexpr int kSidebarDropTargetAcceptingOutlineThickness = 2;
inline constexpr int kSidebarActionCellWidth = 42;
inline constexpr int kSidebarActionCellHeight = 36;
// Header presentation actions are true circles, not narrow 30x36 pills.
inline constexpr int kSidebarHeaderActionSize = 32;
inline constexpr int kSidebarFooterSpacing = 8;
// The saved/temporary boundary is a compact semantic divider, not another tab
// row. Its action and separator consume these shared dimensions so neither the
// host nor the button can reintroduce nested vertical padding.
inline constexpr int kSidebarSectionDividerHeight = 28;
inline constexpr int kSidebarSectionDividerSpacing = kSidebarFooterSpacing;
inline constexpr int kSidebarSectionDividerActionHorizontalInset = 4;
inline constexpr int kSidebarSplitActionGap = 2;
inline constexpr int kSidebarIconSize = 16;

// The normal browsing surface is deliberately inset from the window chrome.
// Besides creating the Arc-like card treatment this inset is part of layout,
// so the renderer receives the real (smaller) viewport instead of being
// visually covered by sidebar or shell chrome.
inline constexpr int kContentCardInset = 8;
inline constexpr int kContentCardCornerRadius = 14;
inline constexpr int kContentCardShadowElevation = 10;
// Split panes live inside the same content card and therefore share its
// curvature. The semantic outline roles distinguish inactive, ordinary active
// and security/focus-highlighted panes without hard-coding a light-only
// palette.
inline constexpr int kSplitPaneCornerRadius = kContentCardCornerRadius;
inline constexpr int kSplitPaneInactiveOutlineThickness = 1;
inline constexpr int kSplitPaneActiveOutlineThickness = 2;
inline constexpr int kSplitPaneHighlightedOutlineThickness = 3;
inline constexpr ui::ColorId kSplitPaneInactiveOutline = kDivider;
inline constexpr ui::ColorId kSplitPaneActiveOutline = kAccent;
inline constexpr ui::ColorId kSplitPaneHighlightedOutline = kFocusRing;
inline constexpr int kNavigationSurfaceHorizontalInset = 10;
inline constexpr int kNavigationSurfaceTopGap = 12;
inline constexpr int kNavigationSurfaceCornerRadius = 14;
inline constexpr int kNavigationSurfaceShadowElevation = 6;
inline constexpr int kNavigationSurfaceRevealOffset = 10;
inline constexpr int kNavigationRevealNotchWidth = 36;
inline constexpr int kNavigationRevealNotchHeight = 12;
inline constexpr int kNavigationRevealNotchVisualHeight = 5;

inline constexpr int kCornerRadiusSmall = 8;
inline constexpr int kCornerRadiusMedium = 12;
inline constexpr int kCornerRadiusLarge = 18;
inline constexpr int kPanelCornerRadius = kCornerRadiusLarge;
inline constexpr int kControlCornerRadius = kCornerRadiusMedium;
inline constexpr int kRowCornerRadius = kCornerRadiusSmall;
inline constexpr int kControlBorderThickness = 1;

inline constexpr int kCommandBarWidth = 600;
inline constexpr int kCommandBarMaximumWidth = 720;
inline constexpr int kCommandBarPanelInset = 14;
inline constexpr int kCommandBarContentWidth =
    kCommandBarWidth - (2 * kCommandBarPanelInset);
inline constexpr int kCommandBarInputHeight = 52;
inline constexpr int kCommandBarResultRowHeight = 46;
inline constexpr int kCommandBarVerticalSpacing = 8;
inline constexpr int kCommandBarResultSpacing = 1;
inline constexpr int kCommandBarResultVerticalInset = 5;
inline constexpr int kCommandBarResultListVerticalInset = 2;
inline constexpr int kCommandBarResultHorizontalInset = 10;
inline constexpr int kCommandBarResultTextSpacing = 9;
inline constexpr int kCommandBarInputHorizontalInset = 14;
inline constexpr int kCommandBarInputSpacing = 10;
inline constexpr int kCommandBarInputIconSize = 18;
inline constexpr int kCommandBarResultIconSize = 18;
inline constexpr int kCommandBarResultIconBoxSize = 20;
inline constexpr int kCommandBarAcceptHintWidth = 16;
inline constexpr int kCommandBarAcceptHintHeight = 20;
inline constexpr int kCommandBarSecondaryTextMaximumWidth = 180;

// Compact, address-bar-anchored developer controls. The surface deliberately
// reuses the same semantic colors, row radius and density as the sidebar so a
// later appearance service can theme both without component-specific code.
inline constexpr int kDeveloperToolkitWidth = 360;
inline constexpr int kDeveloperToolkitInset = 12;
inline constexpr int kDeveloperToolkitRowHeight = 36;
inline constexpr int kDeveloperToolkitControlSpacing = 6;
inline constexpr int kDeveloperToolkitIconSize = 18;
inline constexpr int kDeveloperProfileEditorWidth = 560;
inline constexpr int kDeveloperCookieManagerWidth = 520;
inline constexpr int kDeveloperCookieListMaximumHeight = 280;
inline constexpr int kDeveloperCookieListEditorMaximumHeight = 140;
inline constexpr int kDeveloperCacheStatusWidth = 280;

// Web popup overlays are centered inside the originating WebContents pane.
// Keeping the card, external action rail and motion geometry here ensures the
// popup follows the same density/theme boundary as command bar, sidebar and
// developer surfaces instead of growing a component-local mini design system.
inline constexpr int kPopupCardWidth = 680;
inline constexpr int kPopupCardHeight = 520;
inline constexpr int kPopupCardMinimumWidth = 320;
inline constexpr int kPopupCardMinimumHeight = 220;
inline constexpr int kPopupActionRailWidth = 44;
inline constexpr int kPopupActionRailHeight = 132;
inline constexpr int kPopupActionRailGap = 10;
inline constexpr int kPopupOverlayInset = 24;
inline constexpr int kPopupActionButtonSize = 36;
inline constexpr int kPopupActionIconSize = 18;
inline constexpr int kPopupOriginRowHeight = 36;
inline constexpr int kPopupStatusRowHeight = 32;
inline constexpr int kPopupRevealOffset = 8;
inline constexpr float kPopupScrimOpacity = 0.62f;

// Dialog geometry is shared by workspace/group editors and the recent-links
// hover panel. Keeping the two horizontal insets explicit preserves their
// slightly different visual density while giving the host one source of truth.
inline constexpr int kSidebarDialogWidth = 340;
inline constexpr int kSidebarDialogInset = 14;
inline constexpr int kSidebarRecentLinksDialogInset = 10;
inline constexpr int kSidebarSearchTextHorizontalInset = 8;

// Motion is shared by modal panels and the command surface. Keep these
// deliberately short and subtle; accessibility-sensitive reduced-motion
// policy can later map these values to zero at the central token boundary.
inline constexpr base::TimeDelta kTreeMotionDuration = base::Milliseconds(145);
inline constexpr base::TimeDelta kSidebarRevealDuration =
    base::Milliseconds(180);
inline constexpr base::TimeDelta kSidebarHideDuration = base::Milliseconds(135);
inline constexpr base::TimeDelta kWorkspaceTransitionDuration =
    base::Milliseconds(165);
inline constexpr int kWorkspaceTransitionOffset = 24;
inline constexpr float kWorkspaceTransitionInitialOpacity = 0.86f;
inline constexpr base::TimeDelta kModalFadeInDuration = base::Milliseconds(120);
inline constexpr base::TimeDelta kModalFadeOutDuration = base::Milliseconds(90);
inline constexpr base::TimeDelta kPopupRevealDuration = kModalFadeInDuration;
inline constexpr base::TimeDelta kPopupDismissDuration = kModalFadeOutDuration;
inline constexpr base::TimeDelta kPopupFallbackNoticeDuration =
    base::Milliseconds(220);
inline constexpr base::TimeDelta kNavigationSurfaceRevealDuration =
    base::Milliseconds(180);
inline constexpr base::TimeDelta kNavigationSurfaceHideDuration =
    base::Milliseconds(145);
inline constexpr base::TimeDelta kNavigationSurfaceAutoHideDelay =
    base::Milliseconds(650);
inline constexpr base::TimeDelta kFolderAutoExpandDelay =
    base::Milliseconds(650);
inline constexpr base::TimeDelta kRecentLinksHoverOpenDelay =
    base::Milliseconds(320);
inline constexpr base::TimeDelta kRecentLinksHoverCloseDelay =
    base::Milliseconds(180);
inline constexpr base::TimeDelta kSidebarEdgeRevealHideDelay =
    base::Milliseconds(220);

}  // namespace ahoi::visual_style

#endif  // AHOI_BROWSER_UI_VISUAL_STYLE_H_
