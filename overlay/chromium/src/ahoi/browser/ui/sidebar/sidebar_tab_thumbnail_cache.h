// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_THUMBNAIL_CACHE_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_THUMBNAIL_CACHE_H_

#include <cstdint>
#include <memory>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "ui/gfx/image/image_skia.h"

namespace tabs {
class TabInterface;
}

namespace ahoi::sidebar {

// Keeps Chromium's already-captured tab thumbnail decoded before a native drag
// begins. It observes Chromium's existing hover-card image pipeline and never
// captures or decodes synchronously from a drag callback.
class CachedTabThumbnail final {
 public:
  explicit CachedTabThumbnail(base::RepeatingClosure image_changed_callback);
  CachedTabThumbnail(const CachedTabThumbnail&) = delete;
  CachedTabThumbnail& operator=(const CachedTabThumbnail&) = delete;
  ~CachedTabThumbnail();

  void Observe(tabs::TabInterface* tab);
  void Refresh(tabs::TabInterface* tab);
  const gfx::ImageSkia& image() const;

 private:
  void ObserveImpl(tabs::TabInterface* tab, bool force_refresh);
  void OnThumbnailAvailable(gfx::ImageSkia image);
  void StopObserving(uint64_t generation);

  const base::RepeatingClosure image_changed_callback_;
  scoped_refptr<ThumbnailImage> thumbnail_;
  std::unique_ptr<ThumbnailImage::Subscription> subscription_;
  gfx::ImageSkia image_;
  uint64_t subscription_generation_ = 0;
  base::WeakPtrFactory<CachedTabThumbnail> weak_ptr_factory_{this};
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TAB_THUMBNAIL_CACHE_H_
