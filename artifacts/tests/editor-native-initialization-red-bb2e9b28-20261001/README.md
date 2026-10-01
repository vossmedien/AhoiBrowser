# Editor fixture ContentClient initialization RED

The two-case native editor run reports one CRASH/one NOTRUN in SetUp before
constructing the editor: BackForwardCacheImpl has no ContentBrowserClient in
the pure Views test suite. The next fixture uses Chromium's documented
TestContentClientInitializer and test render-host setup, with orderly context
cleanup before resetting clients. This changes no product implementation.
The corrected fixture typechecks; native execution remains required. Core
127/127 and Mojo20/20 GREEN in the same build remain separately valid.
