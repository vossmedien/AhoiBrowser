# Dependency build workarounds

These patches are not part of Ahoi's Chromium source overlay. A build wrapper
may apply one only to the exact pinned dependency commit while `gn gen` and
`autoninja` run, and must restore a byte-identical clean dependency checkout
before publishing its receipt.

The M152 V8 workaround is bound in
`config/dependency-build-workarounds.json`. It ports the upstream
Inspector-Protocol fix `369afb2ffe24f7c953dcd3eed71b3f1529670732` to V8
commit `6aacaf6256a069ee455142333b7d38cad1c8d6e0`: only absolute Jinja-template
dependencies in `code_generator.py` become relative to the build working
directory. Remove the workaround when the pinned V8 revision contains that
upstream fix.
