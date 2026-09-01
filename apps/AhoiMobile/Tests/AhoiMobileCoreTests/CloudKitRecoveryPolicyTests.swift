import Foundation
import XCTest
@testable import AhoiMobileCore

#if canImport(CloudKit)
import CloudKit

@available(iOS 17.0, macOS 14.0, *)
final class CloudKitRecoveryPolicyTests: XCTestCase {
    func testAccountTransitionDenialKeepsLocalDataIsolated() {
        let plan = CloudKitRecoveryPolicy.accountTransitionPlan(
            allowLocalUpload: false
        )

        XCTAssertTrue(plan.accountTransitionPending)
        XCTAssertFalse(plan.shouldRebuildTransport)
        XCTAssertEqual(plan.declinedStatus?.phase, .accountRequired)
    }

    func testAccountTransitionApprovalRequiresFreshTransport() {
        let plan = CloudKitRecoveryPolicy.accountTransitionPlan(
            allowLocalUpload: true
        )

        XCTAssertFalse(plan.accountTransitionPending)
        XCTAssertTrue(plan.shouldRebuildTransport)
        XCTAssertNil(plan.declinedStatus)
    }

    func testZoneRecoveryRejectsPendingAccountTransition() {
        XCTAssertThrowsError(try CloudKitRecoveryPolicy.validateZoneRecovery(
            accountTransitionPending: true
        )) { error in
            XCTAssertEqual(
                error as? CloudKitSyncProviderError,
                .accountTransitionRequiresConfirmation
            )
        }
        XCTAssertNoThrow(try CloudKitRecoveryPolicy.validateZoneRecovery(
            accountTransitionPending: false
        ))
    }

    func testAuthenticationAndNetworkErrorsMapToFailClosedPhases() {
        XCTAssertEqual(
            status(for: .notAuthenticated).phase,
            .accountRequired
        )
        XCTAssertEqual(
            status(for: .permissionFailure).phase,
            .accountRequired
        )
        XCTAssertEqual(
            status(for: .networkUnavailable).phase,
            .offline
        )
        XCTAssertEqual(
            status(for: .networkFailure).phase,
            .offline
        )
    }

    func testTemporaryAndZoneErrorsScheduleRetryAndPreserveServerHint() {
        for code in [
            CKError.Code.requestRateLimited,
            .serviceUnavailable,
            .zoneBusy,
            .limitExceeded,
            .changeTokenExpired,
            .zoneNotFound,
            .userDeletedZone,
        ] {
            let result = status(for: code, retryAfterSeconds: 17)
            XCTAssertEqual(result.phase, .retryScheduled, "Unexpected phase for \(code)")
            XCTAssertEqual(result.retryAfterSeconds, 17, "Lost retry hint for \(code)")
        }
    }

    func testUnknownCloudAndNonCloudErrorsFailClosed() {
        XCTAssertEqual(status(for: .internalError).phase, .failed)
        let nonCloud = NSError(
            domain: "AhoiMobileTests",
            code: 99,
            userInfo: [NSLocalizedDescriptionKey: "test failure"]
        )
        XCTAssertEqual(
            CloudKitRecoveryPolicy.status(for: nonCloud).phase,
            .failed
        )
    }

    private func status(
        for code: CKError.Code,
        retryAfterSeconds: Double? = nil
    ) -> CloudKitSyncStatus {
        var userInfo: [String: Any] = [:]
        if let retryAfterSeconds {
            userInfo[CKErrorRetryAfterKey] = retryAfterSeconds
        }
        let error = NSError(
            domain: CKErrorDomain,
            code: code.rawValue,
            userInfo: userInfo
        )
        return CloudKitRecoveryPolicy.status(for: error)
    }
}
#endif
