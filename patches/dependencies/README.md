# Dependency build workarounds

These patches are not part of Ahoi's Chromium source overlay. The build wrapper
may apply them only to exact pinned commits and byte-verified target files
while `gn gen` and `autoninja` run. It must restore every target
byte-for-byte before publishing one complete receipt.

The M152 Chromium workaround teaches
`build/rust/gni_impl/rustc_wrapper.py` to preserve rustc's Makefile-escaped
spaces while normalizing and validating depfile paths. It is required when the
canonical checkout path contains spaces because M152 makes build-script
`OUT_DIR` values absolute. Chromium main still carried the same unsupported
space-path parser when this pin was recorded. Remove the workaround only after
the pinned Chromium revision supports the same escaped-path cases.

The M152 V8 workaround is bound in
`config/dependency-build-workarounds.json`. It ports the upstream
Inspector-Protocol fix `369afb2ffe24f7c953dcd3eed71b3f1529670732` to V8
commit `6aacaf6256a069ee455142333b7d38cad1c8d6e0`: only absolute Jinja-template
dependencies in `code_generator.py` become relative to the build working
directory. Remove the workaround when the pinned V8 revision contains that
upstream fix.
