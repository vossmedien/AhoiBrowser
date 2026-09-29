import Foundation
import QuickLook
import SwiftUI

struct MobileDownloadsSheet: View {
    @ObservedObject private var downloads: MobileDownloadCoordinator
    @Binding private var isPresented: Bool
    @Binding private var previewURL: URL?
    private let mode: MobileBrowsingMode

    init(
        downloads: MobileDownloadCoordinator,
        isPresented: Binding<Bool>,
        previewURL: Binding<URL?>,
        mode: MobileBrowsingMode
    ) {
        _downloads = ObservedObject(wrappedValue: downloads)
        _isPresented = isPresented
        _previewURL = previewURL
        self.mode = mode
    }

    var body: some View {
        NavigationStack {
            Group {
                if visibleDownloads.isEmpty {
                    ContentUnavailableView(
                        CompanionL10n.string("browser.downloads.empty", fallback: "No Downloads"),
                        systemImage: "arrow.down.circle"
                    )
                } else {
                    List(visibleDownloads) { download in
                        HStack(spacing: 12) {
                            Image(systemName: download.status == .completed ? "checkmark.circle.fill" : "arrow.down.circle")
                                .foregroundStyle(download.status == .completed ? Color.green : Color.accentColor)
                            VStack(alignment: .leading, spacing: 3) {
                                Text(download.suggestedFilename).lineLimit(1)
                                Text(download.sourceOrigin)
                                    .font(.caption2)
                                    .foregroundStyle(.secondary)
                                    .lineLimit(1)
                                if let fraction = download.progressFraction,
                                   download.status == .starting || download.status == .downloading {
                                    ProgressView(value: fraction)
                                        .accessibilityValue(Text(downloadStatus(download)))
                                        .accessibilityIdentifier(
                                            "browser.downloads.progress.\(download.id.uuidString.lowercased())"
                                        )
                                }
                                Text(downloadStatus(download))
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                    .accessibilityIdentifier(
                                        "browser.downloads.status.\(download.id.uuidString.lowercased())"
                                    )
                            }
                            Spacer()
                            if download.status == .completed, let destination = download.destinationURL {
                                Button {
                                    previewURL = destination
                                } label: {
                                    Image(systemName: "eye")
                                }
                                .accessibilityLabel(CompanionL10n.string(
                                    "browser.download.open",
                                    fallback: "Open download"
                                ))
                                .accessibilityIdentifier(
                                    "browser.downloads.open.\(download.id.uuidString.lowercased())"
                                )
                                ShareLink(item: destination) {
                                    Image(systemName: "square.and.arrow.up")
                                }
                                .accessibilityIdentifier(
                                    "browser.downloads.share.\(download.id.uuidString.lowercased())"
                                )
                            } else if download.status == .starting || download.status == .downloading {
                                Button(role: .destructive) { downloads.cancel(download.id) } label: {
                                    Image(systemName: "xmark.circle")
                                }
                                .accessibilityLabel(CompanionL10n.string(
                                    "browser.download.cancel",
                                    fallback: "Cancel download"
                                ))
                                .accessibilityIdentifier(
                                    "browser.downloads.cancel.\(download.id.uuidString.lowercased())"
                                )
                            } else if downloads.canRetry(download.id) {
                                Button { downloads.retry(download.id) } label: {
                                    Image(systemName: "arrow.clockwise")
                                }
                                .accessibilityLabel(CompanionL10n.string(
                                    "browser.retry",
                                    fallback: "Try Again"
                                ))
                                .accessibilityIdentifier(
                                    "browser.downloads.retry.\(download.id.uuidString.lowercased())"
                                )
                            }
                        }
                        .accessibilityElement(children: .contain)
                        .accessibilityIdentifier(
                            "browser.downloads.row.\(download.id.uuidString.lowercased())"
                        )
                    }
                }
            }
            .navigationTitle(CompanionL10n.string("browser.downloads", fallback: "Downloads"))
            .safeAreaInset(edge: .top) {
                if let recoveryError = downloads.recoveryErrorMessage {
                    Label(recoveryError, systemImage: "exclamationmark.triangle.fill")
                        .font(.footnote)
                        .foregroundStyle(.white)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(Color.red)
                        .accessibilityIdentifier("browser.downloads.recovery-error")
                }
            }
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(CompanionL10n.string("action.done", fallback: "Done")) {
                        isPresented = false
                    }
                    .accessibilityIdentifier("browser.downloads.done")
                }
                if visibleDownloads.contains(where: { [.completed, .failed, .cancelled].contains($0.status) }) {
                    ToolbarItem(placement: .primaryAction) {
                        Button(CompanionL10n.string("action.clear", fallback: "Clear")) {
                            downloads.removeFinished(isPrivate: mode == .privateBrowsing)
                        }
                        .accessibilityIdentifier("browser.downloads.clear")
                    }
                }
            }
        }
        .accessibilityIdentifier("browser.downloads.sheet")
        .quickLookPreview($previewURL)
        .accessibilityAction(.escape) { isPresented = false }
    }

    private var visibleDownloads: [MobileDownloadRecord] {
        downloads.downloads.filter {
            $0.isPrivate == (mode == .privateBrowsing)
        }
    }

    private func downloadStatus(_ download: MobileDownloadRecord) -> String {
        if let error = download.errorMessage { return error }
        switch download.status {
        case .starting:
            return CompanionL10n.string("browser.download.starting", fallback: "Starting…")
        case .downloading:
            if let percent = download.progressPercent {
                return CompanionL10n.format(
                    "browser.download.progress",
                    fallback: "%d%% · %@",
                    percent,
                    formattedDownloadBytes(download)
                )
            }
            return CompanionL10n.format(
                "browser.download.progress.indeterminate",
                fallback: "Downloading · %@",
                formattedDownloadBytes(download)
            )
        case .completed:
            return CompanionL10n.string("browser.download.completed", fallback: "Downloaded")
        case .failed:
            return CompanionL10n.string("browser.download.failed", fallback: "Download failed")
        case .cancelled:
            return CompanionL10n.string("browser.download.cancelled", fallback: "Cancelled")
        }
    }

    private func formattedDownloadBytes(_ download: MobileDownloadRecord) -> String {
        let received = ByteCountFormatter.string(
            fromByteCount: download.bytesReceived,
            countStyle: .file
        )
        guard let total = download.totalBytesExpected else { return received }
        return CompanionL10n.format(
            "browser.download.bytes",
            fallback: "%@ of %@",
            received,
            ByteCountFormatter.string(fromByteCount: total, countStyle: .file)
        )
    }
}
