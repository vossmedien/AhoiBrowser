import Foundation
import XCTest
@testable import AhoiMobileCore

/// The real-device receive test waited ~70 s per keystroke for the app to go
/// idle: a pass ending with records it could not import (a Presence waiting
/// for its Page) asked for a follow-up, which ended the same way and asked
/// again every ~2 s. These tests pin the coalescing and the follow-up gate
/// with a fake engine that delivers fetched events whenever it is fetched.
@MainActor
final class EventDrivenSyncCoalescingTests: XCTestCase {
    func testDefaultSpacingIsAtLeastTwoSeconds() {
        let coalescer = EventDrivenSyncCoalescer(pass: {})
        XCTAssertGreaterThanOrEqual(coalescer.minimumSpacing, .seconds(2))
    }

    func testUnresolvedInboxDoesNotRestartThePassForever() async {
        let engine = FakeEventEngine(unresolvedInbox: ["fetched:presence-without-page"])
        let coalescer = engine.makeCoalescer()

        engine.push("remote-tab")
        await waitUntilIdle(coalescer)
        try? await Task.sleep(for: .milliseconds(250))

        // The push pass plus one follow-up for the new leftovers, then quiet.
        XCTAssertEqual(engine.passes, 2)
        XCTAssertEqual(engine.droppedInPassEvents, engine.passes)
        XCTAssertTrue(coalescer.isIdle)

        // A genuinely new remote change still starts exactly one pass.
        engine.push("next-remote-tab")
        await waitUntilIdle(coalescer)
        try? await Task.sleep(for: .milliseconds(250))
        XCTAssertEqual(engine.passes, 3)
        XCTAssertTrue(coalescer.isIdle)
    }

    func testResolvedLeftoversStopAfterTheirFollowUp() async {
        let engine = FakeEventEngine(unresolvedInbox: ["save:merged-record"])
        engine.resolvesInboxOnPass = 2
        let coalescer = engine.makeCoalescer()

        engine.push("remote-tab")
        await waitUntilIdle(coalescer)
        try? await Task.sleep(for: .milliseconds(250))

        XCTAssertEqual(engine.passes, 2)
        XCTAssertTrue(engine.unresolvedInbox.isEmpty)
    }

    func testBurstOfEventsCoalescesIntoOnePass() async {
        var passes = 0
        let coalescer = EventDrivenSyncCoalescer(
            minimumSpacing: .milliseconds(30),
            maximumSpacing: .milliseconds(120)
        ) { passes += 1 }

        for _ in 0..<20 { coalescer.request() }
        await waitUntilIdle(coalescer)

        XCTAssertEqual(passes, 1)
    }

    func testRequestsDuringAPassQueueOneSpacedPass() async {
        let clock = ContinuousClock()
        var starts: [ContinuousClock.Instant] = []
        var ends: [ContinuousClock.Instant] = []
        let ref = CoalescerReference()
        let coalescer = EventDrivenSyncCoalescer(
            minimumSpacing: .milliseconds(80),
            maximumSpacing: .milliseconds(320)
        ) {
            starts.append(clock.now)
            if starts.count == 1 {
                for _ in 0..<5 { ref.coalescer?.request() }
            }
            try? await Task.sleep(for: .milliseconds(20))
            ends.append(clock.now)
        }
        ref.coalescer = coalescer

        coalescer.request()
        await waitUntilIdle(coalescer)

        XCTAssertEqual(starts.count, 2)
        XCTAssertGreaterThanOrEqual(starts[1] - ends[0], .milliseconds(80))
    }

    func testSelfRequestingPassBacksOffInsteadOfSpinning() async {
        var passes = 0
        let ref = CoalescerReference()
        let coalescer = EventDrivenSyncCoalescer(
            minimumSpacing: .milliseconds(20),
            maximumSpacing: .milliseconds(160)
        ) {
            passes += 1
            ref.coalescer?.request()
        }
        ref.coalescer = coalescer

        coalescer.request()
        try? await Task.sleep(for: .milliseconds(700))
        let observed = passes
        let backoff = coalescer.chainedPasses
        coalescer.cancel()

        // Unspaced this would be hundreds of passes; fixed 20 ms spacing ~35.
        // Doubling to 160 ms bounds it near 20 + 40 + 80 + 160 * 3.
        XCTAssertGreaterThan(observed, 2)
        XCTAssertLessThanOrEqual(observed, 10)
        XCTAssertGreaterThanOrEqual(backoff, 3)
    }

    func testCancelStopsAQueuedPass() async {
        var passes = 0
        let coalescer = EventDrivenSyncCoalescer(
            retryDelay: { .milliseconds(100) }
        ) { passes += 1 }

        coalescer.request()
        await Task.yield()
        coalescer.cancel()
        try? await Task.sleep(for: .milliseconds(200))

        XCTAssertEqual(passes, 0)
        XCTAssertTrue(coalescer.isIdle)
    }

    func testFollowUpGateAsksOncePerNewLeftovers() {
        var gate = BoundedPassFollowUpGate()
        XCTAssertTrue(gate.shouldRequestFollowUp(leftovers: ["fetched:a"]))
        XCTAssertFalse(gate.shouldRequestFollowUp(leftovers: ["fetched:a"]))
        XCTAssertTrue(gate.shouldRequestFollowUp(leftovers: ["fetched:a", "save:b"]))
        XCTAssertFalse(gate.shouldRequestFollowUp(leftovers: ["fetched:a", "save:b"]))
        gate.passCompleted()
        XCTAssertTrue(gate.shouldRequestFollowUp(leftovers: ["fetched:a"]))
    }

    private func waitUntilIdle(
        _ coalescer: EventDrivenSyncCoalescer,
        timeout: Duration = .seconds(3)
    ) async {
        let clock = ContinuousClock()
        let deadline = clock.now + timeout
        await Task.yield()
        while !coalescer.isIdle, clock.now < deadline {
            try? await Task.sleep(for: .milliseconds(10))
        }
        XCTAssertTrue(coalescer.isIdle, "The event-driven sync loop did not settle.")
    }
}

@MainActor
private final class CoalescerReference {
    weak var coalescer: EventDrivenSyncCoalescer?
}

/// Mirrors CloudKitSyncProvider's event contract: fetchChanges() delivers
/// fetchedRecordZoneChanges inside the bounded pass (dropped by the Unbounded
/// guard), a push outside a pass requests a host pass, and finalize asks for
/// a follow-up through the same gate while leftovers remain.
@MainActor
private final class FakeEventEngine {
    var unresolvedInbox: Set<String>
    var resolvesInboxOnPass: Int?
    private(set) var passes = 0
    private(set) var droppedInPassEvents = 0
    private var boundedPassActive = false
    private var serverQueue: [String] = []
    private var gate = BoundedPassFollowUpGate()
    private weak var coalescer: EventDrivenSyncCoalescer?

    init(unresolvedInbox: Set<String>) {
        self.unresolvedInbox = unresolvedInbox
    }

    func makeCoalescer() -> EventDrivenSyncCoalescer {
        let coalescer = EventDrivenSyncCoalescer(
            minimumSpacing: .milliseconds(20),
            maximumSpacing: .milliseconds(80)
        ) { [weak self] in await self?.boundedPass() }
        self.coalescer = coalescer
        return coalescer
    }

    func push(_ recordID: String) {
        serverQueue.append(recordID)
        requestIfUnbounded()
    }

    private func requestIfUnbounded() {
        guard !boundedPassActive else {
            droppedInPassEvents += 1
            return
        }
        coalescer?.request()
    }

    private func boundedPass() async {
        passes += 1
        boundedPassActive = true
        // fetchChanges(): the engine delivers the fetched page as an event.
        serverQueue.removeAll()
        requestIfUnbounded()
        await Task.yield()
        if passes == resolvesInboxOnPass { unresolvedInbox.removeAll() }
        boundedPassActive = false
        // finalizeBoundedSync(): runs after the pass is no longer bounded.
        if unresolvedInbox.isEmpty {
            gate.passCompleted()
        } else if gate.shouldRequestFollowUp(leftovers: unresolvedInbox) {
            requestIfUnbounded()
        }
    }
}
