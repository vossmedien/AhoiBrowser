# Header lifetime editor source, 1 October 2026

Tracked native editor source now offers request/response header lifetime:
"Until this tab is closed" or the existing "Across browser restarts" option.
The temporary choice unchecks/disables both header sync controls; returning to
persistent does not silently restore prior opt-in. Saving gates both flags again.
A lifetime change cancels an outstanding style compilation/secret commit and
rejects its old callback, preventing a persistent snapshot from being committed
after the user chose temporary rules. Draft controls remain in the editor.

Header controls moved into `developer_profile_editor_headers.cc` and its existing
UI GN source set. Existing header text area height is preserved, source files
stay below 800 lines. Patch 0090 supplies three English/German messages; the
canonical series owns its order. No unique changes live only in Chromium.

GRIT's official parser/ID allocator/rc_header generator produced the staged
header using actual development defines and resource ID files. Its unchanged
baseline is byte-identical to the existing generated header. New IDs are from
the actual patched GRD, not invented CPP defines. XML, official translation
fingerprints and patch applicability pass. No shared checkout/output refresh or
resource packaging was performed. The initial allowlist-option mismatch is
retained separately; it is not a product/header pass.

Three final source syntax checks pass on pinned Clang, one job, against that
owned genuine GRIT header. The initial SimpleComboboxModel item-type and test
raw_ptr/protected-callback errors are preserved. The corrected tests use
Chromium's real ComboboxTestApi user-selection path. Three new editor regression
methods are written/syntax-checked, **not executed** (the existing two are not
relabelled as a new native pass).

The fresh installed split opportunity was terminal/deferred because an actual
foreign xcodebuild existed, despite idle >=90 seconds. It launched no browser
and claimed no E2E lock. This is not visible lifecycle acceptance.

Next: real Mojo temporary-header lifetime cases; then one guarded exact
candidate and required native/installed gates after fresh capacity/ownership
checks. Installed 9acb43c8/M154 remains unchanged. No secret/Keychain, external
API, simulator, signing/install or release action occurs in this source step.
