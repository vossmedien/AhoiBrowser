// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/tab_switcher/tab_switcher_view.h"

#include <algorithm>
#include <utility>

#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check_op.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_number_conversions.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"

namespace ahoi::tab_switcher {

namespace {

constexpr int kGap = 12;
constexpr int kColumnCount = static_cast<int>(kColumns);
constexpr int kTileWidth = (kPanelWidth - 2 * visual_style::kDialogPadding -
                            (kColumnCount - 1) * kGap) /
                           kColumnCount;
constexpr int kPreviewHeight = 96;
constexpr int kTileHeight = kPreviewHeight + 52;
constexpr int kTileRadius = visual_style::kCornerRadiusMedium;
constexpr int kMaxGridHeight = 3 * kTileHeight + 2 * kGap + 3 * 28;

bool German() {
  return base::i18n::GetConfiguredLocale().starts_with("de");
}

std::u16string Text(const char16_t* de, const char16_t* en) {
  return German() ? de : en;
}

std::u16string SectionTitle(Section section) {
  switch (section) {
    case Section::kSaved:
      return Text(u"Angeheftet und gespeichert", u"Pinned and saved");
    case Section::kTemporary:
      return Text(u"Temporäre Tabs", u"Temporary tabs");
    case Section::kSplits:
      return Text(u"Splits", u"Splits");
  }
}

std::u16string TileTitle(const Entry& entry) {
  if (entry.tab_ids.size() < 2) {
    return entry.title;
  }
  return entry.title + u" · " + base::NumberToString16(entry.tab_ids.size()) +
         Text(u" Ansichten", u" views");
}

cc::PaintFlags Fill(SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  return flags;
}

std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        int style,
                                        ui::ColorId color) {
  auto label = std::make_unique<views::Label>(text, views::style::CONTEXT_LABEL,
                                              style);
  // The glass panel has a translucent layer, so text must use grayscale AA.
  label->SetSubpixelRenderingEnabled(false);
  label->SetEnabledColor(color);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

// A key hint chip ("↵") and its meaning.
std::unique_ptr<views::View> KeyHint(const std::u16string& keys,
                                     const std::u16string& meaning) {
  auto hint = std::make_unique<views::View>();
  hint->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  auto* chip = hint->AddChildView(
      MakeLabel(keys, views::style::STYLE_SECONDARY, visual_style::kText));
  chip->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(1, 6)));
  chip->SetBackground(views::CreateRoundedRectBackground(
      visual_style::kSelectedSurface, visual_style::kCornerRadiusSmall / 2));
  hint->AddChildView(MakeLabel(meaning, views::style::STYLE_SECONDARY,
                               visual_style::kMutedText));
  return hint;
}

}  // namespace

std::u16string PanelTitle() {
  return Text(u"Tabs wechseln", u"Switch tabs");
}

// One entry: preview on top, favicon, title and host below. The keyboard
// focus is the switcher's selection.
class TabSwitcherTile final : public views::Button {
  METADATA_HEADER(TabSwitcherTile, views::Button)

 public:
  TabSwitcherTile(const Entry& entry,
                  const EntryVisual& visual,
                  PressedCallback pressed,
                  base::RepeatingCallback<bool(const ui::KeyEvent&)> key)
      : views::Button(std::move(pressed)),
        title_(TileTitle(entry)),
        host_(entry.host),
        favicon_(visual.favicon),
        key_callback_(std::move(key)),
        thumbnail_(base::BindRepeating(&TabSwitcherTile::SchedulePaint,
                                       base::Unretained(this))) {
    SetPreferredSize(gfx::Size(kTileWidth, kTileHeight));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetName(title_ + u", " + host_);
    if (visual.preview_tab) {
      thumbnail_.Observe(visual.preview_tab);
    }
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    return key_callback_.Run(event) || views::Button::OnKeyPressed(event);
  }

  void OnFocus() override {
    views::Button::OnFocus();
    ScrollViewToVisible();
    SchedulePaint();
  }
  void OnBlur() override {
    views::Button::OnBlur();
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    const gfx::RectF bounds(GetLocalBounds());
    const bool focused = HasFocus();
    canvas->DrawRoundRect(
        bounds, kTileRadius,
        Fill(colors->GetColor(focused ? visual_style::kDropTargetSurface
                                      : visual_style::kRaisedSurface)));

    // Preview, cropped to fill its area; the favicon stands in until
    // Chromium has captured the tab.
    gfx::Rect preview(GetLocalBounds());
    preview.Inset(gfx::Insets::TLBR(6, 6, 0, 6));
    preview.set_height(kPreviewHeight - 6);
    {
      gfx::ScopedCanvas clip(canvas);
      canvas->ClipPath(SkPath::RRect(gfx::RectFToSkRect(gfx::RectF(preview)),
                                     kTileRadius - 4, kTileRadius - 4),
                       /*do_anti_alias=*/true);
      const gfx::ImageSkia& image = thumbnail_.image();
      if (!image.isNull() && image.width() > 0 && image.height() > 0) {
        const float scale =
            std::max(static_cast<float>(preview.width()) / image.width(),
                     static_cast<float>(preview.height()) / image.height());
        const int src_w = std::min(image.width(),
                                   static_cast<int>(preview.width() / scale));
        const int src_h = std::min(image.height(),
                                   static_cast<int>(preview.height() / scale));
        canvas->DrawImageInt(image, (image.width() - src_w) / 2, 0, src_w,
                             src_h, preview.x(), preview.y(), preview.width(),
                             preview.height(), /*filter=*/true);
      } else {
        canvas->DrawRect(gfx::RectF(preview),
                         Fill(colors->GetColor(visual_style::kHoverSurface)));
        const gfx::ImageSkia icon = favicon_.Rasterize(colors);
        if (!icon.isNull()) {
          canvas->DrawImageInt(icon, preview.CenterPoint().x() - 8,
                               preview.CenterPoint().y() - 8);
        }
      }
    }

    const gfx::ImageSkia icon = favicon_.Rasterize(colors);
    const int text_x = 12 + 16 + 10;
    if (!icon.isNull()) {
      canvas->DrawImageInt(icon, 0, 0, icon.width(), icon.height(), 12,
                           kPreviewHeight + 16, 16, 16, /*filter=*/true);
    }
    const gfx::FontList& base_font = views::Label::GetDefaultFontList();
    const gfx::Rect title_rect(text_x, kPreviewHeight + 6,
                               width() - text_x - 10, 20);
    const gfx::Rect host_rect(text_x, kPreviewHeight + 26,
                              width() - text_x - 10, 18);
    canvas->DrawStringRectWithFlags(
        title_, base_font.DeriveWithWeight(gfx::Font::Weight::MEDIUM),
        colors->GetColor(visual_style::kText), title_rect,
        gfx::Canvas::NO_SUBPIXEL_RENDERING);
    canvas->DrawStringRectWithFlags(
        host_, base_font.DeriveWithSizeDelta(-1),
        colors->GetColor(visual_style::kMutedText), host_rect,
        gfx::Canvas::NO_SUBPIXEL_RENDERING);

    if (focused) {
      cc::PaintFlags ring = Fill(colors->GetColor(visual_style::kAccent));
      ring.setStyle(cc::PaintFlags::kStroke_Style);
      ring.setStrokeWidth(2.0f);
      gfx::RectF outline = bounds;
      outline.Inset(1.0f);
      canvas->DrawRoundRect(outline, kTileRadius - 1, ring);
    }
  }

 private:
  const std::u16string title_;
  const std::u16string host_;
  const ui::ImageModel favicon_;
  const base::RepeatingCallback<bool(const ui::KeyEvent&)> key_callback_;
  sidebar::CachedTabThumbnail thumbnail_;
};

BEGIN_METADATA(TabSwitcherTile)
END_METADATA

TabSwitcherView::TabSwitcherView(std::u16string workspace_name,
                                 PrefService* prefs,
                                 Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(visual_style::kDialogPadding), kGap));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetName(PanelTitle());

  auto* header = AddChildView(std::make_unique<views::View>());
  header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  header->AddChildView(MakeLabel(PanelTitle(), views::style::STYLE_HEADLINE_4,
                                 visual_style::kText));
  if (!workspace_name.empty()) {
    header->AddChildView(MakeLabel(Text(u"Workspace ", u"Workspace ") +
                                       workspace_name,
                                   views::style::STYLE_SECONDARY,
                                   visual_style::kMutedText));
  }

  scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->ClipHeightTo(0, kMaxGridHeight);
  grid_ = scroll_view_->SetContents(std::make_unique<views::View>());
  grid_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));

  auto* footer = AddChildView(std::make_unique<views::View>());
  footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::TLBR(8, 0, 0, 0),
      20));
  footer->AddChildView(KeyHint(u"↑ ↓ ← →", Text(u"wählen", u"choose")));
  footer->AddChildView(KeyHint(u"↵", Text(u"öffnen", u"open")));
  footer->AddChildView(KeyHint(u"W", Text(u"schließen", u"close")));
  footer->AddChildView(KeyHint(u"esc", Text(u"abbrechen", u"cancel")));

  appearance_signal_source_ =
      std::make_unique<appearance::AppearanceRuntimeSignalSource>(
          prefs, base::BindRepeating(&TabSwitcherView::OnAppearanceChanged,
                                     weak_ptr_factory_.GetWeakPtr()));
  OnAppearanceChanged(appearance_signal_source_->policy());
}

TabSwitcherView::~TabSwitcherView() = default;

void TabSwitcherView::SetEntries(std::vector<Entry> entries,
                                 std::vector<EntryVisual> visuals,
                                 size_t focus) {
  CHECK_EQ(entries.size(), visuals.size());
  tiles_.clear();
  grid_->RemoveAllChildViews();
  entries_ = std::move(entries);
  views::View* row = nullptr;
  for (size_t index = 0; index < entries_.size(); ++index) {
    const bool new_section =
        index == 0 || entries_[index].section != entries_[index - 1].section;
    if (new_section) {
      auto* label = grid_->AddChildView(
          MakeLabel(SectionTitle(entries_[index].section),
                    views::style::STYLE_EMPHASIZED, visual_style::kText));
      label->SetBorder(views::CreateEmptyBorder(
          gfx::Insets::TLBR(index == 0 ? 0 : 8, 0, 0, 0)));
    }
    if (new_section || row->children().size() == kColumns) {
      row = grid_->AddChildView(std::make_unique<views::View>());
      row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kGap));
    }
    tiles_.push_back(row->AddChildView(std::make_unique<TabSwitcherTile>(
        entries_[index], visuals[index],
        base::BindRepeating(callbacks_.activate, index),
        base::BindRepeating(&TabSwitcherView::HandleKey,
                            base::Unretained(this)))));
  }
  focus_ = entries_.empty() ? 0 : std::min(focus, entries_.size() - 1);
  PreferredSizeChanged();
}

void TabSwitcherView::FocusCurrent() {
  if (focus_ < tiles_.size()) {
    tiles_[focus_]->RequestFocus();
  }
}

void TabSwitcherView::FocusNext() {
  SetFocusIndex(MoveFocus(entries_, focus_, Move::kNext));
}

void TabSwitcherView::ReapplyAppearance() {
  OnAppearanceChanged(appearance_signal_source_->policy());
}

void TabSwitcherView::SetFocusIndex(size_t index) {
  focus_ = index;
  FocusCurrent();
}

bool TabSwitcherView::HandleKey(const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed || entries_.empty()) {
    return false;
  }
  const bool plain = !event.IsCommandDown() && !event.IsControlDown() &&
                     !event.IsAltDown() && !event.IsShiftDown();
  switch (event.key_code()) {
    case ui::VKEY_LEFT:
      SetFocusIndex(MoveFocus(entries_, focus_, Move::kLeft));
      return true;
    case ui::VKEY_RIGHT:
      SetFocusIndex(MoveFocus(entries_, focus_, Move::kRight));
      return true;
    case ui::VKEY_UP:
      SetFocusIndex(MoveFocus(entries_, focus_, Move::kUp));
      return true;
    case ui::VKEY_DOWN:
      SetFocusIndex(MoveFocus(entries_, focus_, Move::kDown));
      return true;
    case ui::VKEY_TAB:
    case ui::VKEY_T:
      if (event.IsControlDown()) {
        FocusNext();
        return true;
      }
      return false;
    case ui::VKEY_RETURN:
      callbacks_.activate.Run(focus_);
      return true;
    case ui::VKEY_W:
      if (plain || event.IsCommandDown()) {
        callbacks_.close.Run(focus_);
        return true;
      }
      return false;
    default:
      return false;
  }
}

void TabSwitcherView::OnAppearanceChanged(
    const appearance::GlassPolicy& policy) {
  panel_material_.Apply(this, policy);
}

}  // namespace ahoi::tab_switcher
