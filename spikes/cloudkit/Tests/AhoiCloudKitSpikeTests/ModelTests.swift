import XCTest
@testable import AhoiCloudKitSpike

final class ModelTests: XCTestCase {
    func testHLCMergePreservesCausalityWhenWallClockMovesBackwards() throws {
        let localDevice = DeviceID(rawValue: UUID(uuidString: "00000000-0000-0000-0000-000000000001")!)
        let remoteDevice = DeviceID(rawValue: UUID(uuidString: "00000000-0000-0000-0000-000000000002")!)
        let local = HybridLogicalClock(
            physicalMilliseconds: 1_000,
            logicalCounter: 4,
            nodeID: localDevice
        )
        let remote = HybridLogicalClock(
            physicalMilliseconds: 1_000,
            logicalCounter: 8,
            nodeID: remoteDevice
        )

        let merged = try local.merging(remote, at: 900)

        XCTAssertEqual(merged.physicalMilliseconds, 1_000)
        XCTAssertEqual(merged.logicalCounter, 9)
        XCTAssertGreaterThan(merged, local)
        XCTAssertGreaterThan(merged, remote)
    }

    func testOrderKeyCanBeInsertedBetweenSiblingsWithoutRenumbering() throws {
        let actor = DeviceID()
        let lower = try OrderKey(components: [10], tieBreaker: actor)
        let upper = try OrderKey(components: [11], tieBreaker: actor)

        let inserted = try OrderKey.between(lower, upper, tieBreaker: DeviceID())

        XCTAssertLessThan(lower, inserted)
        XCTAssertLessThan(inserted, upper)
        XCTAssertEqual(lower.components, [10])
        XCTAssertEqual(upper.components, [11])
    }

    func testTombstoneWinsWhenPhysicalAndLogicalCoordinatesTie() throws {
        let deviceA = DeviceID(rawValue: UUID(uuidString: "00000000-0000-0000-0000-000000000001")!)
        let deviceB = DeviceID(rawValue: UUID(uuidString: "00000000-0000-0000-0000-000000000002")!)
        let liveTimestamp = HybridLogicalClock(
            physicalMilliseconds: 42,
            logicalCounter: 1,
            nodeID: deviceB
        )
        let deletedTimestamp = HybridLogicalClock(
            physicalMilliseconds: 42,
            logicalCounter: 1,
            nodeID: deviceA
        )
        let live = try VersionedValue(
            value: "live",
            modifiedAt: liveTimestamp,
            originatingDevice: deviceB
        )
        let deleted = try VersionedValue(
            value: "deleted",
            modifiedAt: deletedTimestamp,
            originatingDevice: deviceA,
            isTombstone: true
        )
        let resolver = LastWriterWinsResolver()

        XCTAssertTrue(resolver.resolve(live, deleted).isTombstone)
        XCTAssertTrue(resolver.resolve(deleted, live).isTombstone)

        let liveFromA = try VersionedValue(
            value: "device-a",
            modifiedAt: deletedTimestamp,
            originatingDevice: deviceA
        )
        let liveFromB = try VersionedValue(
            value: "device-b",
            modifiedAt: liveTimestamp,
            originatingDevice: deviceB
        )
        XCTAssertEqual(resolver.resolve(liveFromA, liveFromB).value, "device-b")
        XCTAssertEqual(resolver.resolve(liveFromB, liveFromA).value, "device-b")
    }

    func testConflictResolutionIsCommutativeForDifferentValuesAndIdenticalMetadata() throws {
        let metadata = identicalConflictMetadata()
        let alpha = try VersionedValue(
            value: "alpha",
            modifiedAt: metadata.clock,
            originatingDevice: metadata.device
        )
        let omega = try VersionedValue(
            value: "omega",
            modifiedAt: metadata.clock,
            originatingDevice: metadata.device
        )
        let resolver = LastWriterWinsResolver()

        XCTAssertNotEqual(alpha.conflictTieBreaker, omega.conflictTieBreaker)
        XCTAssertEqual(resolver.resolve(alpha, omega), resolver.resolve(omega, alpha))
    }

    func testConflictResolutionIsAssociativeForDifferentValuesAndIdenticalMetadata() throws {
        let metadata = identicalConflictMetadata()
        let alpha = try VersionedValue(
            value: "alpha",
            modifiedAt: metadata.clock,
            originatingDevice: metadata.device
        )
        let middle = try VersionedValue(
            value: "middle",
            modifiedAt: metadata.clock,
            originatingDevice: metadata.device
        )
        let omega = try VersionedValue(
            value: "omega",
            modifiedAt: metadata.clock,
            originatingDevice: metadata.device
        )
        let resolver = LastWriterWinsResolver()

        let leftGrouped = resolver.resolve(resolver.resolve(alpha, middle), omega)
        let rightGrouped = resolver.resolve(alpha, resolver.resolve(middle, omega))
        XCTAssertEqual(leftGrouped, rightGrouped)
    }

    func testSupportedScalarCanonicalizationMatchesGoldenWireBytes() throws {
        XCTAssertEqual(
            try "A".stableConflictCanonicalBytes().hex,
            "01000000000000000141"
        )
        XCTAssertEqual(
            try true.stableConflictCanonicalBytes().hex,
            "02000000000000000101"
        )
        XCTAssertEqual(
            try Int64(-1).stableConflictCanonicalBytes().hex,
            "030000000000000008ffffffffffffffff"
        )
        XCTAssertEqual(
            try Int(-1).stableConflictCanonicalBytes(),
            try Int64(-1).stableConflictCanonicalBytes()
        )
        XCTAssertEqual(
            try UInt64(1).stableConflictCanonicalBytes().hex,
            "0400000000000000080000000000000001"
        )
        XCTAssertEqual(
            try UInt(1).stableConflictCanonicalBytes(),
            try UInt64(1).stableConflictCanonicalBytes()
        )
        XCTAssertEqual(
            try UInt32(1).stableConflictCanonicalBytes(),
            try UInt64(1).stableConflictCanonicalBytes()
        )
        XCTAssertEqual(
            try Data([0x00, 0xff]).stableConflictCanonicalBytes().hex,
            "06000000000000000200ff"
        )

        let composed = "é"
        let decomposed = "e\u{301}"
        XCTAssertEqual(
            try composed.stableConflictCanonicalBytes(),
            try decomposed.stableConflictCanonicalBytes()
        )

        let device = DeviceID(
            rawValue: UUID(uuidString: "90000000-0000-0000-0000-000000000009")!
        )
        XCTAssertEqual(
            try device.stableConflictCanonicalBytes().hex,
            "05000000000000002439303030303030302d303030302d303030302d303030302d303030303030303030303039"
        )

        let encrypted = EncryptedValue(
            keyVersion: 1,
            nonce: Data(repeating: 1, count: 12),
            ciphertextAndTag: Data(repeating: 2, count: 16)
        )
        XCTAssertEqual(
            try encrypted.stableConflictCanonicalBytes().hex,
            "200000000000000004000000000000001401000000000000000b4145532d3235362d47434d00000000000000110400000000000000080000000000000001000000000000001506000000000000000c010101010101010101010101000000000000001906000000000000001002020202020202020202020202020202"
        )
    }

    func testCollectionCanonicalizationIsPermutationStableForSetsAndOrderedForArrays() throws {
        let permutations = [
            ["alpha", "beta", "gamma"],
            ["alpha", "gamma", "beta"],
            ["beta", "alpha", "gamma"],
            ["beta", "gamma", "alpha"],
            ["gamma", "alpha", "beta"],
            ["gamma", "beta", "alpha"],
        ]
        let setKeys = try permutations.map {
            try ConflictTieBreaker(canonicalizing: Set($0))
        }

        XCTAssertEqual(Set(setKeys).count, 1)
        XCTAssertNotEqual(
            try ConflictTieBreaker(canonicalizing: permutations[0]),
            try ConflictTieBreaker(canonicalizing: permutations[1])
        )

        let none: String? = nil
        let some: String? = "alpha"
        XCTAssertNotEqual(
            try ConflictTieBreaker(canonicalizing: none),
            try ConflictTieBreaker(canonicalizing: some)
        )
    }

    func testSetCanonicalizationFailsClosedOnCollidingOptInImplementation() {
        let values: Set<CollidingCanonicalValue> = [.init(id: 1), .init(id: 2)]

        XCTAssertThrowsError(try values.stableConflictCanonicalBytes()) { error in
            XCTAssertEqual(
                error as? ConflictCanonicalizationError,
                .duplicateCanonicalSetElement
            )
        }
    }

    func testRecoveryMetadataRetainsTreePlacementWithoutPagePayload() throws {
        let device = DeviceID()
        let order = try OrderKey.between(nil, nil, tieBreaker: device)
        let recovery = RecoveryMetadata(
            entityID: UUID(),
            tombstoneID: UUID(),
            recoveryParentID: UUID(),
            recoveryOrderKey: order,
            recoveredAt: .init(physicalMilliseconds: 7, nodeID: device)
        )

        let encoded = try JSONEncoder().encode(recovery)
        let decoded = try JSONDecoder().decode(RecoveryMetadata.self, from: encoded)
        XCTAssertEqual(decoded, recovery)
    }

    func testMalformedDecodedOrderKeysAreRejectedWithoutProcessAbort() throws {
        let device = DeviceID()
        let empty = try JSONSerialization.data(withJSONObject: [
            "components": [],
            "tieBreaker": ["rawValue": device.rawValue.uuidString],
        ])
        let tooDeep = try JSONSerialization.data(withJSONObject: [
            "components": Array(repeating: 1, count: OrderKey.maximumDepth + 1),
            "tieBreaker": ["rawValue": device.rawValue.uuidString],
        ])

        XCTAssertThrowsError(try JSONDecoder().decode(OrderKey.self, from: empty))
        XCTAssertThrowsError(try JSONDecoder().decode(OrderKey.self, from: tooDeep))
        XCTAssertThrowsError(try OrderKey(components: [], tieBreaker: device))
    }

    func testExhaustedHLCCounterThrowsInsteadOfCrashing() {
        let device = DeviceID()
        let exhausted = HybridLogicalClock(
            physicalMilliseconds: 100,
            logicalCounter: UInt32.max,
            nodeID: device
        )

        XCTAssertThrowsError(try exhausted.ticking(at: 100)) { error in
            XCTAssertEqual(error as? HybridLogicalClockError, .logicalCounterExhausted)
        }
        XCTAssertThrowsError(try exhausted.merging(exhausted, at: 99)) { error in
            XCTAssertEqual(error as? HybridLogicalClockError, .logicalCounterExhausted)
        }
    }

    private func identicalConflictMetadata() -> (
        clock: HybridLogicalClock,
        device: DeviceID
    ) {
        let device = DeviceID(
            rawValue: UUID(uuidString: "90000000-0000-0000-0000-000000000009")!
        )
        return (
            HybridLogicalClock(
                physicalMilliseconds: 9_000,
                logicalCounter: 9,
                nodeID: device
            ),
            device
        )
    }
}

private struct CollidingCanonicalValue: ConflictCanonicalizable {
    let id: Int

    func stableConflictCanonicalBytes() throws -> Data {
        Data([0])
    }
}

private extension Data {
    var hex: String {
        map { String(format: "%02x", $0) }.joined()
    }
}
