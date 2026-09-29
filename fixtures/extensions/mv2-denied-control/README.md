# MV2 denied control

This intentionally minimal Manifest V2 extension is a negative security
control. AhoiBrowser must reject it before and after the narrow uBlock Origin
Classic compatibility path is implemented. A successful installation is a test
failure; this fixture is never shipped. Even if its manifest is edited to use
the pinned uBO identity, unpacked origin and missing exact package/key hashes
must keep it blocked. It is not **Official GitHub release** evidence.
