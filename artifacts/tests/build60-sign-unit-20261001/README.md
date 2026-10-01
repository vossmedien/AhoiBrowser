# Build 60 signing and unit gate

Frozen source d0688d88; no repeated compilation. The owner signer from
46f5d4ae uses the existing Apple Development Keychain identity with independent
public-leaf codeSign trust evaluation. Signing, deep/strict stamped bundle
verification and the original frozen repository provenance all pass.

20 native unit programs executed with 2 jobs and no retries: 19 exit 0;
1311 successful cases and one failure. SidebarBookmarkShelfViewTest's native
context deletion case observes an already closed menu. Its first failure is
preserved in receipt.json. This is not an installed-browser acceptance pass.
The installer was not invoked, so installed Build 59 remains unchanged.

The single-job focused diagnosis and then the complete sidebar suite are
prepared under `.work/agent-queue/60/sidebar-serial/`, gated on a 90-second
idle interval and owner UI/build locks. Their result must be checked live;
no assumed flake, retry-to-green or waived installation gate.
