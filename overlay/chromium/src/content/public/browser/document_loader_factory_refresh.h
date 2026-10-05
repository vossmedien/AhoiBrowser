// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_DOCUMENT_LOADER_FACTORY_REFRESH_H_
#define CONTENT_PUBLIC_BROWSER_DOCUMENT_LOADER_FACTORY_REFRESH_H_

#include "content/common/content_export.h"

namespace content {
class RenderFrameHost;

// Recreates an active, live document's native subresource factories and those
// of its registered dedicated workers after an embedder network-policy change.
// Native builders retain origin/security parameters and interception order.
// Does not navigate, replay scripts, or update shared/service worker ownership.
CONTENT_EXPORT void RecreateDocumentSubresourceLoaderFactories(
    RenderFrameHost& frame);

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_DOCUMENT_LOADER_FACTORY_REFRESH_H_
