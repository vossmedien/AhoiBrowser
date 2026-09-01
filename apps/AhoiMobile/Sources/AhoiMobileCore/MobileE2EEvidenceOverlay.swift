import CryptoKit
import Foundation
import SwiftUI

/// Visible, inert evidence for an explicitly opted-in UI-test launch. The app
/// reads source/build identity from its own Info.plist; the runner-provided
/// receipt digest only binds that externally verified receipt to this launch.
struct MobileE2EEvidenceOverlay<Content: View>: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    let content: Content

    var body: some View {
        content.overlay(alignment: .topLeading) {
            VStack(alignment: .leading, spacing: 3) {
                if let candidateEvidence {
                    evidenceChip(
                        candidateEvidence.label,
                        identifier: "browser.e2e.candidate-binding",
                        value: candidateEvidence.value
                    )
                }
                if exposesReduceMotionEvidence {
                    evidenceChip(
                        reduceMotion ? "Reduce Motion active" : "Standard motion active",
                        identifier: "browser.e2e.reduce-motion",
                        value: reduceMotion ? "enabled" : "disabled"
                    )
                }
            }
            .padding(4)
            .allowsHitTesting(false)
        }
    }

    private func evidenceChip(
        _ label: String,
        identifier: String,
        value: String
    ) -> some View {
        Text(label)
            .font(.caption2.monospaced().weight(.semibold))
            .lineLimit(1)
            .padding(.horizontal, 6)
            .padding(.vertical, 3)
            .foregroundStyle(.primary)
            .background(.regularMaterial, in: Capsule())
            .accessibilityIdentifier(identifier)
            .accessibilityValue(value)
    }

    private var candidateEvidence: (label: String, value: String)? {
        let environment = ProcessInfo.processInfo.environment
        guard let expectedCommit = environment[
            "AHOI_MOBILE_E2E_EXPECTED_SOURCE_COMMIT"
        ],
              let expectedBuildMode = environment[
                "AHOI_MOBILE_E2E_EXPECTED_BUILD_MODE"
              ],
              let receiptSHA = environment[
                "AHOI_MOBILE_E2E_CANDIDATE_RECEIPT_SHA256"
              ],
              environment[
                "AHOI_MOBILE_E2E_EXPECTED_EXECUTABLE_SHA256"
              ] != nil,
              !expectedCommit.isEmpty,
              !expectedBuildMode.isEmpty,
              !receiptSHA.isEmpty,
              let executableSHA = MobileE2EIntrinsicEvidence.executableSHA256 else {
            return nil
        }
        guard let sourceCommit = configuredInfoValue("AhoiSourceCommit"),
              let buildMode = configuredInfoValue("AhoiBuildMode"),
              let bundleID = Bundle.main.bundleIdentifier else {
            return nil
        }
        let label = "E2E \(sourceCommit.prefix(8)) · \(buildMode) · \(receiptSHA.prefix(8))"
        let value = "bundleId=\(bundleID);sourceCommit=\(sourceCommit);"
            + "buildMode=\(buildMode);receiptSha256=\(receiptSHA);"
            + "executableSha256=\(executableSHA)"
        return (label, value)
    }

    private var exposesReduceMotionEvidence: Bool {
        ProcessInfo.processInfo.arguments.filter {
            $0 == "-AhoiUITestReduceMotionEvidence"
        }.count == 1
    }

    private func configuredInfoValue(_ key: String) -> String? {
        guard let value = Bundle.main.object(forInfoDictionaryKey: key) as? String else {
            return nil
        }
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, !trimmed.contains("$(") else { return nil }
        return trimmed
    }
}

private enum MobileE2EIntrinsicEvidence {
    static let executableSHA256: String? = {
        guard let executableURL = Bundle.main.executableURL,
              let data = try? Data(contentsOf: executableURL, options: .mappedIfSafe) else {
            return nil
        }
        return SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }()
}
