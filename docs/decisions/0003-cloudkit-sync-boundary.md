# ADR 0003: CloudKit private-zone sync with an explicit denylist

Status: accepted for Phase 0 validation

## Decision

Use a provider abstraction with CloudKit private custom zones first. Sync only
enumerated organization/history/appearance records. Encrypt designated values
before transport. Never sync browser secrets or storage listed in
`config/sync-policy.json`.

## Consequences

The Mac and native companion can interoperate without an Ahoi account/server.
Conflict resolution, encryption key recovery, CloudKit quotas, account changes,
and GPL/App Store distribution remain explicit implementation/release gates.
