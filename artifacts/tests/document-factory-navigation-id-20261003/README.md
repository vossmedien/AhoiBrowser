# Installed document-header failure and source correction

Actual native AX/HID tests on installed b6d3237758e10e010cf1ae52cefb57b099b79abb,
Chromium154.0.8037.93. Root granted a fresh bounded GUI window; capacity before
launch was81.86% CPU idle,54% memory available, pressure1, no compiler/browser.

The first corrected run created a native two-pane split and typed a real draft.
Both document fetches use their warm cache (counts1/1) but miss configured request
and response headers. The existing dedicated worker passes both header directions
and warm cache. The toolkit step stopped because the address bar was collapsed;
no cache-control acceptance is claimed. The harness now reveals the native address
bar. A separate subsequent run yielded to TerminalPID1058/HIDidle0 before further
input. Both attempts failed honestly, retained their partial results, released the
lock and cleaned every owned browser/fixture process. Installed identities stayed
unchanged. The complete GUI slot was explicitly returned to Root.

Source diagnosis: CommitNavigation creates its document factory before RFH assigns
navigation_id_ at commit. Its ContentClientParams previously omitted the optional
navigation ID, so the developer proxy bound the previous RFH ID. After commit its
existing equality guard rejects document requests. Workers created after commit
bind the current ID, explaining the observed difference.

Patch0094 carries the matching navigation ID alongside origin/config for pending,
last-committed and refreshed factories through private native helpers into the
existing ContentClientParams parameter. Failed-navigation factories use their
request ID. Origin/frame/generation/navigation checks remain unchanged; no RFH
public-vtable, profile, permission, sandbox or extension ownership changes.
Two browser regressions exercise document headers across a subsequent navigation
and native defaults with toolkit disabled.

Validation: git apply --check --whitespace=error-all against the exact integrated
checkout PASS; shell syntax and git diff --check PASS. The correction and its two
regressions are NOT compiled, executed or installed. Existing72 is still the
visible-test candidate. Full cache12, exact new-candidate acceptance, focused native
regressions and the complete feature/DoD matrix remain open. No rebuild was started.
Crest106/110 reviewed: no product patch applies; separate H3 lease remains pending.

Subsequent independent headless diagnosis is permitted by the Master's explicit
technical-limit exception: the visible journey could not complete after actual
foreign Terminal focus. This is diagnostic evidence, not a waived GUI criterion.
Thirty-five core cases yielded29 SUCCESS, one fixture CRASH, five SKIPPED. The
crash was a second BindNewPipeAndPassReceiver on the fixture's already-bound Mojo
remote, not the product refresh seam. Source now resets the first completed
request remote before the separate policy-restoration request. A remaining-only
run executed the five skipped core cases and five new Sync cases: all10 SUCCESS.
No unchanged green case or crashed binary case was repeated. Aggregate result:
39/40 distinct native cases SUCCESS; corrected fixture case still requires rebuild
and rerun. Both edited test translation units pass pinned-Clang syntax (2/2,
serial, no checkout/binary changes). Native RFH patch itself has application-check
proof only. First malformed private argv extraction failed before C++ processing;
its immutable attempt is retained privately, corrected exact argument sequence
passed. All owned processes and locks are released. No new build or GUI started.
