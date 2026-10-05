// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/document_loader_factory_refresh.h"

#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/worker_host/dedicated_worker_hosts_for_document.h"
#include "content/public/browser/render_frame_host.h"

namespace content {

void RecreateDocumentSubresourceLoaderFactories(RenderFrameHost& frame) {
  auto* native = RenderFrameHostImpl::FromFrameToken(frame.GetGlobalFrameToken());
  if (native != &frame || !native->IsActive() || !native->IsRenderFrameLive()) {
    return;
  }
  native->UpdateSubresourceLoaderFactories();
  if (auto* workers =
          DedicatedWorkerHostsForDocument::GetForCurrentDocument(native)) {
    workers->UpdateSubresourceLoaderFactories();
  }
}

}  // namespace content
