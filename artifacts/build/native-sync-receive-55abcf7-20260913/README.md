# Receiver with token-bound original setting authority

Final source55abcf76498dcc8629f94e5ecebde89a4269ecf3 is455 plus the specifically
required per-setting read-authority correction. Existing category opt-out revokes
BrowserSettingConsent leases without changing the CloudKit transport generation.
The original setting leases are now captured at wake and delivery selection,
bound to that selected token, checked after PostTask, and held through Store
precommit/ACK/OnIncomingState. Off/on cannot revive that old authority; unapproved
settings remain cached. No schema/wire/golden or permission model change.

The separate source-only preparation tree was
/private/tmp/ahoi-receive-authority.6O80m8/repo. After the original455 build60531
completed EXIT0, the known build worktree was advanced cleanly to55ab. There was
no injection into a running source snapshot, no second build path and no455
CloudKit installation. Original455 receipt SHA
8620b84bbcdbfd830b8c13f8319113c4c476cd5f5af9ba8fbb2018616e91d17c is preserved.

Final guarded product-only run61607 uses3jobs, start72%CPU idle/70GiB free, same
checkout/out and immutable Development/bba configuration. Installed26 and all
original/entitled/rollback artifacts remain protected. No final peer/runtime
acceptance is claimed before the actual coordinated open-window arrival.
