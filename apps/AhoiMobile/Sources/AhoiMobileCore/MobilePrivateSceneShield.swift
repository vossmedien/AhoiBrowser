import SwiftUI
import UIKit

@MainActor
final class MobileBackgroundTaskLease {
    private let name: String
    private var identifier: UIBackgroundTaskIdentifier = .invalid

    init(name: String) {
        self.name = name
    }

    func begin() {
        guard identifier == .invalid else { return }
        identifier = UIApplication.shared.beginBackgroundTask(withName: name) { [weak self] in
            Task { @MainActor [weak self] in
                self?.end()
            }
        }
    }

    func end() {
        guard identifier != .invalid else { return }
        let activeIdentifier = identifier
        identifier = .invalid
        UIApplication.shared.endBackgroundTask(activeIdentifier)
    }
}

/// SwiftUI presentations live above their presenting view, so a root overlay
/// alone does not protect an already-open sheet or alert in the app-switcher
/// snapshot. This marker installs a matching opaque shield at the owning
/// window level and removes it with the conditional SwiftUI cover.
@MainActor
struct MobilePrivateSceneShield: UIViewRepresentable {
    let title: String
    let message: String

    func makeCoordinator() -> Coordinator {
        Coordinator(title: title, message: message)
    }

    func makeUIView(context: Context) -> MobilePrivateSceneMarkerView {
        let marker = MobilePrivateSceneMarkerView()
        marker.backgroundColor = .clear
        marker.isUserInteractionEnabled = false
        marker.onWindowChange = { [weak coordinator = context.coordinator] window in
            coordinator?.install(in: window)
        }
        return marker
    }

    func updateUIView(_ uiView: MobilePrivateSceneMarkerView, context: Context) {
        context.coordinator.update(title: title, message: message)
        context.coordinator.install(in: uiView.window)
    }

    static func dismantleUIView(
        _ uiView: MobilePrivateSceneMarkerView,
        coordinator: Coordinator
    ) {
        uiView.onWindowChange = nil
        coordinator.remove()
    }

    @MainActor
    final class Coordinator {
        private let shield = UIView()
        private let titleLabel = UILabel()
        private let messageLabel = UILabel()

        init(title: String, message: String) {
            shield.backgroundColor = .systemBackground
            shield.isOpaque = true
            shield.isUserInteractionEnabled = true
            shield.isAccessibilityElement = true
            shield.accessibilityViewIsModal = true
            shield.accessibilityIdentifier = "browser.private-window-shield"
            shield.layer.zPosition = 10_000

            let icon = UIImageView(image: UIImage(
                systemName: "hand.raised.fill",
                withConfiguration: UIImage.SymbolConfiguration(
                    pointSize: 34,
                    weight: .semibold
                )
            ))
            icon.tintColor = .systemPurple
            icon.contentMode = .scaleAspectFit
            icon.isAccessibilityElement = false

            titleLabel.font = .preferredFont(forTextStyle: .headline)
            titleLabel.textAlignment = .center
            titleLabel.adjustsFontForContentSizeCategory = true

            messageLabel.font = .preferredFont(forTextStyle: .subheadline)
            messageLabel.textColor = .secondaryLabel
            messageLabel.textAlignment = .center
            messageLabel.numberOfLines = 0
            messageLabel.adjustsFontForContentSizeCategory = true

            let stack = UIStackView(arrangedSubviews: [icon, titleLabel, messageLabel])
            stack.axis = .vertical
            stack.alignment = .center
            stack.spacing = 12
            stack.translatesAutoresizingMaskIntoConstraints = false
            shield.addSubview(stack)
            NSLayoutConstraint.activate([
                icon.widthAnchor.constraint(equalToConstant: 48),
                icon.heightAnchor.constraint(equalToConstant: 48),
                stack.centerXAnchor.constraint(equalTo: shield.centerXAnchor),
                stack.centerYAnchor.constraint(equalTo: shield.centerYAnchor),
                stack.leadingAnchor.constraint(
                    greaterThanOrEqualTo: shield.leadingAnchor,
                    constant: 28
                ),
                stack.trailingAnchor.constraint(
                    lessThanOrEqualTo: shield.trailingAnchor,
                    constant: -28
                ),
            ])
            update(title: title, message: message)
        }

        func update(title: String, message: String) {
            titleLabel.text = title
            messageLabel.text = message
            shield.accessibilityLabel = "\(title). \(message)"
        }

        func install(in window: UIWindow?) {
            guard let window else {
                remove()
                return
            }
            window.endEditing(true)
            if shield.superview !== window {
                shield.removeFromSuperview()
                shield.frame = window.bounds
                shield.autoresizingMask = [.flexibleWidth, .flexibleHeight]
                window.addSubview(shield)
            } else {
                shield.frame = window.bounds
                window.bringSubviewToFront(shield)
            }
        }

        func remove() {
            shield.removeFromSuperview()
        }
    }
}

@MainActor
final class MobilePrivateSceneMarkerView: UIView {
    var onWindowChange: (@MainActor (UIWindow?) -> Void)?

    override func didMoveToWindow() {
        super.didMoveToWindow()
        onWindowChange?(window)
    }
}
