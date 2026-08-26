// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/size.h"

namespace ahoi::sidebar {

CachedTabThumbnail::CachedTabThumbnail(
    base::RepeatingClosure image_changed_callback)
    : image_changed_callback_(std::move(image_changed_callback)) {}

CachedTabThumbnail::~CachedTabThumbnail() = default;

void CachedTabThumbnail::Observe(tabs::TabInterface* tab) {
  ObserveImpl(tab, false);
}

void CachedTabThumbnail::Refresh(tabs::TabInterface* tab) {
  ObserveImpl(tab, true);
}

const gfx::ImageSkia& CachedTabThumbnail::image() const {
  return image_;
}

void CachedTabThumbnail::ObserveImpl(tabs::TabInterface* tab,
                                     bool force_refresh) {
  ThumbnailTabHelper* helper =
      tab && tab->GetContents()
          ? ThumbnailTabHelper::FromWebContents(tab->GetContents())
          : nullptr;
  scoped_refptr<ThumbnailImage> thumbnail =
      helper ? helper->thumbnail() : nullptr;
  if (thumbnail == thumbnail_ && !force_refresh) {
    return;
  }

  subscription_.reset();
  ++subscription_generation_;
  if (thumbnail != thumbnail_) {
    thumbnail_ = std::move(thumbnail);
    image_ = gfx::ImageSkia();
  }
  if (!thumbnail_) {
    image_changed_callback_.Run();
    return;
  }

  subscription_ = thumbnail_->Subscribe();
  subscription_->SetSizeHint(gfx::Size(224, 126));
  subscription_->SetUncompressedImageCallback(base::BindRepeating(
      &CachedTabThumbnail::OnThumbnailAvailable, base::Unretained(this)));
  thumbnail_->RequestThumbnailImage();
}

void CachedTabThumbnail::OnThumbnailAvailable(gfx::ImageSkia image) {
  image_ = std::move(image);
  image_changed_callback_.Run();
  // ThumbnailImage::Subscription forbids destruction from its callback.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&CachedTabThumbnail::StopObserving,
                                weak_ptr_factory_.GetWeakPtr(),
                                subscription_generation_));
}

void CachedTabThumbnail::StopObserving(uint64_t generation) {
  if (generation == subscription_generation_) {
    subscription_.reset();
  }
}

}  // namespace ahoi::sidebar
