import Foundation

/// Serializes event-driven sync requests (CKSyncEngine pushes, local enqueues
/// and bounded-pass follow-ups) into at most one running and one queued pass.
///
/// Requests that arrive while a pass waits are absorbed by it. A request that
/// arrives while a pass runs, or within `minimumSpacing` after it ended, is
/// served only after the spacing; each such chained pass doubles the spacing
/// up to `maximumSpacing`. A pass that keeps asking for itself therefore
/// backs off instead of keeping the main actor busy, and the spacing returns
/// to the minimum once a pass ends without a chained request.
@MainActor
final class EventDrivenSyncCoalescer {
    typealias Sleeper = @Sendable (Duration) async -> Void

    let minimumSpacing: Duration
    let maximumSpacing: Duration
    private let retryDelay: @MainActor () -> Duration?
    private let shouldRun: @MainActor () -> Bool
    private let pass: @MainActor () async -> Void
    private let sleep: Sleeper
    private let clock = ContinuousClock()
    private var task: Task<Void, Never>?
    private var generation: UInt64 = 0
    private var requested = false
    private var chainedRequest = false
    private(set) var passRunning = false
    private(set) var chainedPasses = 0
    private(set) var completedPasses = 0
    private var lastPassEnd: ContinuousClock.Instant?

    init(
        minimumSpacing: Duration = .seconds(2),
        maximumSpacing: Duration = .seconds(60),
        retryDelay: @escaping @MainActor () -> Duration? = { nil },
        shouldRun: @escaping @MainActor () -> Bool = { true },
        sleep: @escaping Sleeper = { try? await Task.sleep(for: $0) },
        pass: @escaping @MainActor () async -> Void
    ) {
        self.minimumSpacing = minimumSpacing
        self.maximumSpacing = max(maximumSpacing, minimumSpacing)
        self.retryDelay = retryDelay
        self.shouldRun = shouldRun
        self.sleep = sleep
        self.pass = pass
    }

    var isIdle: Bool { task == nil }

    func request() {
        if passRunning || endedWithinMinimumSpacing() {
            chainedRequest = true
        }
        requested = true
        guard task == nil else { return }
        generation &+= 1
        let taskGeneration = generation
        task = Task { @MainActor [weak self] in
            await Task.yield()
            await self?.drain(generation: taskGeneration)
            guard let self, self.generation == taskGeneration else { return }
            self.task = nil
        }
    }

    func cancel() {
        generation &+= 1
        task?.cancel()
        task = nil
        requested = false
        chainedRequest = false
        passRunning = false
        chainedPasses = 0
        lastPassEnd = nil
    }

    private func drain(generation taskGeneration: UInt64) async {
        while !Task.isCancelled, generation == taskGeneration,
              requested, shouldRun() {
            if chainedRequest {
                chainedPasses = min(chainedPasses + 1, 16)
            } else {
                chainedPasses = 0
            }
            let delay = max(remainingSpacing(), retryDelay() ?? .zero)
            if delay > .zero {
                await sleep(delay)
                guard !Task.isCancelled, generation == taskGeneration,
                      shouldRun() else { return }
            }
            // Requests absorbed while waiting belong to this pass.
            requested = false
            chainedRequest = false
            passRunning = true
            await pass()
            guard generation == taskGeneration else { return }
            passRunning = false
            completedPasses += 1
            lastPassEnd = clock.now
        }
    }

    private func currentSpacing() -> Duration {
        var spacing = minimumSpacing
        for _ in 0..<chainedPasses where spacing < maximumSpacing {
            spacing = min(spacing * 2, maximumSpacing)
        }
        return spacing
    }

    private func remainingSpacing() -> Duration {
        guard let lastPassEnd else { return .zero }
        let elapsed = clock.now - lastPassEnd
        let spacing = currentSpacing()
        return elapsed < spacing ? spacing - elapsed : .zero
    }

    private func endedWithinMinimumSpacing() -> Bool {
        guard let lastPassEnd else { return false }
        return clock.now - lastPassEnd < minimumSpacing
    }
}
