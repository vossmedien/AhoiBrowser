# Owned dedicated workers, CDN cache and native menu test source

Native M154 DedicatedWorkerHost passes its actual ancestor RenderFrameHost to
the worker subresource hook (kUseAncestorRenderFrameForWorker is enabled by
default). Patch 0089 now routes that frame-owned type through the existing
frame/navigation/activation-bound adapter. Shared workers pass no frame, and
service worker factories remain outside this adapter; they require their own
native owner binding. No process-ID guess or engine flag override is added.

Cache-off is document-owned for HTTP(S) CDN resources too. A cross-origin
request receives only the cache policy; UA/header rules and Keychain references
are absent. Two added Mojo cases prove the expected boundaries when executed;
three real-browser cases are written for dedicated worker header/cache behavior,
disabled native defaults and CDN cache bypass without header leakage. They use
local embedded servers and synthetic markers, not real secrets/accounts.

Build 60's isolated native menu RED is retained separately. The source test now
waits for parent activation, schedules the command after RunMenuAt initializes,
and asserts a showing native MenuController before deleting its source node.
It preserves the real command, stock-item and post-deletion assertions. This
is a source correction, not a bypass or an executed green result.

Six pinned-Clang translation units and full composition pass. No link/native
runtime or installed acceptance is claimed. Next guarded exact candidate must
run the changed menu case/sidebar suite, document Mojo and the real worker
browser cases, followed by affected installed journeys. Crest 106/110 reviewed,
still deferred to their actual performance candidate/lease.
