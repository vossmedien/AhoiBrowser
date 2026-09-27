# Generated sync field/type catalogue (H1.4)

Source of truth: `config/sync-format.json`. Regenerate with
`python3 tools/sync_conformance/generate_field_catalogue.py`; `--check` refuses
stale outputs. The generated C++ header, Swift table and JSON manifest carry
all declared entity IDs, data-class names, wire-model versions and ordered
field-group lists, plus a hash of those contract entries.

These are test/reference assets, not replacements for product codecs. No build
file includes them automatically, and no capability or model-version default
changes. A Sync-owner decision is still required before replacing handwritten
product mappings/codecs. Value types inside each field group are not declared
by the current config; this generator does not invent such a schema.

The existing `field_groups.py` gate compares the handwritten C++/Swift maps
against the contract. `test_sync_conformance_catalogue.py` adds generation and
freshness coverage and is discovered by `scripts/test-repository.sh` with the
other repository tests. Native compilation and the new repository test methods
are pending the current no-further-runs window; generated files are not proof
that every entity's merge behavior has conformance vectors.
