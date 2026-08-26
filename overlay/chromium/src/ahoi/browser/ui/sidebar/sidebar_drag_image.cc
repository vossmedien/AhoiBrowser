// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>

#include "ahoi/browser/ui/visual_style.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/text_elider.h"
#include "ui/views/drag_utils.h"
#include "ui/views/paint_info.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

namespace {

constexpr int kVisiblePreviewWidth = 224;
constexpr int kCursorCardGap = 12;
#if BUILDFLAG(IS_MAC)
// DragDropClientMac centers the complete NSDraggingItem and currently ignores
// OSExchangeData's cursor offset. A transparent leading canvas keeps the
// visible 224px card wholly to the right of the pointer without changing
// Chromium's global drag behavior.
constexpr int kPreviewCanvasWidth =
    2 * (kVisiblePreviewWidth + kCursorCardGap);
constexpr int kVisiblePreviewX = kPreviewCanvasWidth / 2 + kCursorCardGap;
#else
constexpr int kPreviewCanvasWidth = kVisiblePreviewWidth;
constexpr int kVisiblePreviewX = 0;
#endif
constexpr int kFallbackHeight = 54;
constexpr int kThumbnailHeight = 168;
constexpr int kCardInset = 6;
constexpr int kHeaderHeight = 38;
constexpr int kFaviconSize = 16;
constexpr int kThumbnailGap = 3;

gfx::ImageSkia PrepareThumbnail(const gfx::ImageSkia& source,
                                const gfx::Size& target_size) {
  if (source.isNull() || source.size().IsEmpty() || target_size.IsEmpty()) {
    return gfx::ImageSkia();
  }
  const double source_ratio =
      static_cast<double>(source.width()) / source.height();
  const double target_ratio =
      static_cast<double>(target_size.width()) / target_size.height();
  gfx::Rect crop(source.size());
  if (source_ratio > target_ratio) {
    const int width =
        std::max(1, static_cast<int>(source.height() * target_ratio));
    crop.set_x((source.width() - width) / 2);
    crop.set_width(width);
  } else {
    const int height =
        std::max(1, static_cast<int>(source.width() / target_ratio));
    crop.set_y((source.height() - height) / 2);
    crop.set_height(height);
  }
  gfx::ImageSkia cropped =
      gfx::ImageSkiaOperations::ExtractSubset(source, crop);
  gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
      cropped, skia::ImageOperations::ResizeMethod::RESIZE_GOOD, target_size);
  return gfx::ImageSkiaOperations::CreateImageWithRoundRectClip(5, resized);
}

class SidebarDragPreviewView final : public views::View {
 public:
  SidebarDragPreviewView(const ui::ColorProvider* colors,
                         gfx::ImageSkia favicon,
                         std::u16string title,
                         std::vector<gfx::ImageSkia> thumbnails)
      : card_color_(colors->GetColor(visual_style::kRaisedSurface)),
        text_color_(colors->GetColor(visual_style::kText)),
        muted_color_(colors->GetColor(visual_style::kMutedText)),
        divider_color_(colors->GetColor(visual_style::kDivider)),
        favicon_(std::move(favicon)),
        title_(std::move(title)),
        thumbnails_(std::move(thumbnails)),
        represented_tab_count_(std::max<size_t>(1u, thumbnails_.size())) {
    std::erase_if(thumbnails_,
                  [](const gfx::ImageSkia& image) { return image.isNull(); });
    SetBounds(0, 0, kPreviewCanvasWidth,
              thumbnails_.empty() ? kFallbackHeight : kThumbnailHeight);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    gfx::RectF card(kVisiblePreviewX, 0, kVisiblePreviewWidth, height());
    card.Inset(gfx::InsetsF(kCardInset));

    // A small layered shadow remains visible in the bitmap itself. Native drag
    // windows do not include View layers when snapshotting their contents.
    cc::PaintFlags shadow;
    shadow.setAntiAlias(true);
    shadow.setStyle(cc::PaintFlags::kFill_Style);
    shadow.setColor(SkColorSetARGB(30, 0, 0, 0));
    gfx::RectF shadow_bounds = card;
    shadow_bounds.Offset(0.0f, 2.0f);
    shadow_bounds.Inset(-2.0f);
    canvas->DrawRoundRect(shadow_bounds, 11.0f, shadow);

    cc::PaintFlags fill = shadow;
    fill.setColor(card_color_);
    canvas->DrawRoundRect(card, 9.0f, fill);
    cc::PaintFlags border;
    border.setAntiAlias(true);
    border.setStyle(cc::PaintFlags::kStroke_Style);
    border.setStrokeWidth(1.0f);
    border.setColor(divider_color_);
    gfx::RectF border_bounds = card;
    border_bounds.Inset(0.5f);
    canvas->DrawRoundRect(border_bounds, 8.5f, border);

    PaintHeader(canvas, card);
    if (!thumbnails_.empty()) {
      PaintThumbnails(canvas, card);
    }
  }

 private:
  void PaintHeader(gfx::Canvas* canvas, const gfx::RectF& card) {
    const gfx::Rect icon_bounds(static_cast<int>(card.x()) + 10,
                                static_cast<int>(card.y()) + 11, kFaviconSize,
                                kFaviconSize);
    if (!favicon_.isNull()) {
      canvas->DrawImageInt(favicon_, 0, 0, favicon_.width(), favicon_.height(),
                           icon_bounds.x(), icon_bounds.y(),
                           icon_bounds.width(), icon_bounds.height(), true);
    } else {
      cc::PaintFlags fallback;
      fallback.setAntiAlias(true);
      fallback.setStyle(cc::PaintFlags::kStroke_Style);
      fallback.setStrokeWidth(1.25f);
      fallback.setColor(muted_color_);
      gfx::RectF page(icon_bounds);
      page.Inset(gfx::InsetsF(1.5f));
      canvas->DrawRoundRect(page, 2.0f, fallback);
    }

    std::u16string display_title = title_;
    if (represented_tab_count_ > 1u) {
      display_title.append(u"  +");
      display_title.append(base::NumberToString16(represented_tab_count_ - 1u));
    }
    const gfx::FontList font;
    const int available_width =
        static_cast<int>(card.width()) - 10 - kFaviconSize - 8 - 10;
    display_title =
        gfx::ElideText(display_title, font, available_width, gfx::ELIDE_TAIL);
    const gfx::Rect title_bounds(icon_bounds.right() + 8,
                                 static_cast<int>(card.y()), available_width,
                                 kHeaderHeight);
    canvas->DrawStringRectWithFlags(
        display_title, font, text_color_, title_bounds,
        gfx::Canvas::TEXT_ALIGN_LEFT | gfx::Canvas::NO_ELLIPSIS);
  }

  void PaintThumbnails(gfx::Canvas* canvas, const gfx::RectF& card) {
    const gfx::Rect content(
        static_cast<int>(card.x()) + 7,
        static_cast<int>(card.y()) + kHeaderHeight,
        static_cast<int>(card.width()) - 14,
        static_cast<int>(card.height()) - kHeaderHeight - 7);
    if (thumbnails_.size() == 1u) {
      const gfx::ImageSkia thumbnail =
          PrepareThumbnail(thumbnails_.front(), content.size());
      canvas->DrawImageInt(thumbnail, content.x(), content.y());
      return;
    }

    if (thumbnails_.size() > 3u) {
      const size_t columns = 2;
      const size_t rows = (thumbnails_.size() + columns - 1) / columns;
      const int content_height = std::max(
          0, content.height() - static_cast<int>(rows - 1) * kThumbnailGap);
      for (size_t index = 0; index < thumbnails_.size(); ++index) {
        const size_t row = index / columns;
        const size_t column = index % columns;
        const int x = content.x() + static_cast<int>(column) *
                                        ((content.width() - kThumbnailGap) / 2 +
                                         kThumbnailGap);
        const int y =
            content.y() +
            static_cast<int>(row) *
                (content_height / static_cast<int>(rows) + kThumbnailGap);
        const int right = content.x() +
                          static_cast<int>(column + 1) *
                              (content.width() - kThumbnailGap) / 2 +
                          static_cast<int>(column) * kThumbnailGap;
        const int bottom = content.y() +
                           static_cast<int>(row + 1) * content_height /
                               static_cast<int>(rows) +
                           static_cast<int>(row) * kThumbnailGap;
        const gfx::Rect tile(x, y, std::max(0, right - x),
                             std::max(0, bottom - y));
        canvas->DrawImageInt(PrepareThumbnail(thumbnails_[index], tile.size()),
                             tile.x(), tile.y());
      }
      return;
    }

    const int first_width = (content.width() - kThumbnailGap) / 2;
    const gfx::Rect first(content.x(), content.y(), first_width,
                          content.height());
    const gfx::ImageSkia first_image =
        PrepareThumbnail(thumbnails_[0], first.size());
    canvas->DrawImageInt(first_image, first.x(), first.y());

    const gfx::Rect remainder(first.right() + kThumbnailGap, content.y(),
                              content.right() - first.right() - kThumbnailGap,
                              content.height());
    if (thumbnails_.size() == 2u) {
      const gfx::ImageSkia second =
          PrepareThumbnail(thumbnails_[1], remainder.size());
      canvas->DrawImageInt(second, remainder.x(), remainder.y());
      return;
    }

    const int top_height = (remainder.height() - kThumbnailGap) / 2;
    const gfx::Rect top(remainder.x(), remainder.y(), remainder.width(),
                        top_height);
    const gfx::Rect bottom(remainder.x(), top.bottom() + kThumbnailGap,
                           remainder.width(),
                           remainder.bottom() - top.bottom() - kThumbnailGap);
    const gfx::ImageSkia second = PrepareThumbnail(thumbnails_[1], top.size());
    const gfx::ImageSkia third =
        PrepareThumbnail(thumbnails_[2], bottom.size());
    canvas->DrawImageInt(second, top.x(), top.y());
    canvas->DrawImageInt(third, bottom.x(), bottom.y());
  }

  const SkColor card_color_;
  const SkColor text_color_;
  const SkColor muted_color_;
  const SkColor divider_color_;
  const gfx::ImageSkia favicon_;
  const std::u16string title_;
  std::vector<gfx::ImageSkia> thumbnails_;
  const size_t represented_tab_count_;
};

}  // namespace

gfx::ImageSkia CreateSidebarDragImage(
    views::Widget* source_widget,
    const ui::ColorProvider* color_provider,
    const gfx::ImageSkia& favicon,
    const std::u16string& title,
    const std::vector<gfx::ImageSkia>& cached_thumbnails) {
  if (!source_widget || !color_provider) {
    return gfx::ImageSkia();
  }
  SidebarDragPreviewView preview(color_provider, favicon, title,
                                 cached_thumbnails);
  SkBitmap bitmap;
  const float raster_scale = views::ScaleFactorForDragFromWidget(source_widget);
  preview.Paint(views::PaintInfo::CreateRootPaintInfo(
      ui::CanvasPainter(&bitmap, preview.size(), raster_scale,
                        SK_ColorTRANSPARENT, /*is_pixel_canvas=*/true)
          .context(),
      preview.size()));
  return gfx::ImageSkia::CreateFromBitmap(bitmap, raster_scale);
}

gfx::Vector2d GetSidebarDragImageCursorOffset(
    const gfx::ImageSkia& image,
    const gfx::Point& press_point) {
  if (image.isNull() || image.size().IsEmpty()) {
    return gfx::Vector2d();
  }
#if BUILDFLAG(IS_MAC)
  // Kept correct for a future DragDropClientMac implementation that starts
  // honoring the provider offset; today AppKit uses the image center.
  return gfx::Vector2d(
      image.width() / 2,
      std::clamp(press_point.y(), 0, std::max(image.height() - 1, 0)));
#else
  return gfx::Vector2d(
      -kCursorCardGap,
      std::clamp(press_point.y(), 0, std::max(image.height() - 1, 0)));
#endif
}

}  // namespace ahoi::sidebar
