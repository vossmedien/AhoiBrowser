# Genuine encrypted-cookie/restart GREEN, 2 October 2026

Exact installed Build71/6bf4c233/M154, windowed, idle247, fresh owned profile,
actual native keychain/encryption provider, no mock or sandbox exception.
Owned HTTP fixture sets one synthetic persistent HttpOnly/SameSite-Lax cookie.
Server receives it on a following request, then again after actual native close
and a second browser process launch on the same profile. Both documents commit,
both native exits0, cleanup complete. No real user cookies, passwords or key values
are read/displayed/exported; HTTP evidence stores only a synthetic-match boolean.

After the first native shutdown, read-only SQLite inspection of this profile's
cookie database finds the synthetic row: clear value empty, encrypted value67
bytes, prefixv10, positive persistent expiry. Only length/prefix are retained;
the actual encrypted data/key is not exported. Successful post-restart server
delivery additionally proves the native decrypt/read path for this fixture.
State/check/source/command/raw hashes are bound in receipt.json.

This proves that synthetic cookie's real encrypted persistence/restart on this
candidate. It does not prove all cookie types, two-account/workspace isolation,
full Sync or complete Master/release acceptance. Other visible scopes retain
their own gates. No release/device/API/trading or foreign-profile changes occur.
