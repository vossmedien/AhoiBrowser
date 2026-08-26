// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"

#include <memory>
#include <vector>

#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

namespace {

gfx::ImageSkia MakeImage(int width, int height, SkColor color) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(width, height);
  bitmap.eraseColor(color);
  return gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
}

class SidebarDragImageTest : public views::ViewsTestBase {};

TEST_F(SidebarDragImageTest, BuildsCompactFallbackAndThumbnailCards) {
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  views::View* contents =
      widget->SetContentsView(std::make_unique<views::View>());
  widget->SetSize(gfx::Size(320, 240));
  widget->Show();
  ASSERT_TRUE(contents->GetColorProvider());

  const gfx::ImageSkia favicon = MakeImage(16, 16, SK_ColorBLUE);
  const gfx::ImageSkia fallback = CreateSidebarDragImage(
      widget.get(), contents->GetColorProvider(), favicon, u"Ahoi", {});
  ASSERT_FALSE(fallback.isNull());
#if BUILDFLAG(IS_MAC)
  EXPECT_EQ(gfx::Size(472, 54), fallback.size());
#else
  EXPECT_EQ(gfx::Size(224, 54), fallback.size());
#endif
  const gfx::Vector2d offset =
      GetSidebarDragImageCursorOffset(fallback, gfx::Point(20, 16));
#if BUILDFLAG(IS_MAC)
  EXPECT_EQ(236, offset.x());
#else
  EXPECT_EQ(-12, offset.x());
#endif
  EXPECT_EQ(16, offset.y());

  std::vector<gfx::ImageSkia> thumbnails = {MakeImage(320, 180, SK_ColorRED),
                                            MakeImage(320, 180, SK_ColorGREEN),
                                            MakeImage(320, 180, SK_ColorBLUE)};
  const gfx::ImageSkia split =
      CreateSidebarDragImage(widget.get(), contents->GetColorProvider(),
                             favicon, u"Split", thumbnails);
  ASSERT_FALSE(split.isNull());
#if BUILDFLAG(IS_MAC)
  EXPECT_EQ(gfx::Size(472, 168), split.size());
#else
  EXPECT_EQ(gfx::Size(224, 168), split.size());
#endif

  thumbnails.push_back(MakeImage(320, 180, SK_ColorYELLOW));
  const gfx::ImageSkia four_pane =
      CreateSidebarDragImage(widget.get(), contents->GetColorProvider(),
                             favicon, u"Four pane", thumbnails);
  ASSERT_FALSE(four_pane.isNull());
#if BUILDFLAG(IS_MAC)
  EXPECT_EQ(gfx::Size(472, 168), four_pane.size());
#else
  EXPECT_EQ(gfx::Size(224, 168), four_pane.size());
#endif
}

}  // namespace

}  // namespace ahoi::sidebar
