# Dependency build workarounds

These patches are not part of Ahoi's Chromium source overlay. The build wrapper
may apply them only to exact pinned commits and byte-verified target files
while `gn gen` and `autoninja` run. It must restore every target
byte-for-byte before publishing one complete receipt.

The M153 Chromium workaround teaches
`build/rust/gni_impl/rustc_wrapper.py` to preserve Makefile-escaped spaces and
to escape rustc's raw absolute build-root prefix before normalizing and
validating depfile paths. It is required when the canonical checkout path
contains spaces because M153 makes build-script `OUT_DIR` values absolute.
The pinned M153 source still carries the same unsupported space-path parser.
Remove the workaround only after the pinned Chromium revision
supports both escaped paths and raw absolute `OUT_DIR` paths.

The M153 V8 workaround is bound in
`config/dependency-build-workarounds.json`. It ports the upstream
Inspector-Protocol fix `369afb2ffe24f7c953dcd3eed71b3f1529670732` to V8
commit `d1fed5cd7e3b114dea70f18b20d26f816322833d`: only absolute Jinja-template
dependencies in `code_generator.py` become relative to the build working
directory. Remove the workaround when the pinned V8 revision contains that
upstream fix.
