import SwiftUI
import UIKit

/// Keeps Escape in the responder chain of a presented SwiftUI hierarchy.
/// Focused UIKit-backed controls can consume the key before an ancestor
/// `onKeyPress` handler sees it, while this parent controller remains in the
/// first responder's controller chain.
@MainActor
struct MobileHardwareEscapeContainer<Content: View>: UIViewControllerRepresentable {
    private let onEscape: () -> Void
    private let content: Content

    init(
        onEscape: @escaping () -> Void,
        @ViewBuilder content: () -> Content
    ) {
        self.onEscape = onEscape
        self.content = content()
    }

    func makeUIViewController(context: Context) -> MobileHardwareEscapeController {
        MobileHardwareEscapeController(
            content: AnyView(content),
            onEscape: onEscape
        )
    }

    func updateUIViewController(
        _ uiViewController: MobileHardwareEscapeController,
        context: Context
    ) {
        uiViewController.update(content: AnyView(content), onEscape: onEscape)
    }
}

@MainActor
final class MobileHardwareEscapeController: UIViewController {
    private let hostingController: UIHostingController<AnyView>
    private var onEscape: () -> Void

    init(content: AnyView, onEscape: @escaping () -> Void) {
        hostingController = UIHostingController(rootView: content)
        self.onEscape = onEscape
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func loadView() {
        let container = UIView()
        container.backgroundColor = .clear
        view = container

        hostingController.view.backgroundColor = .clear
        hostingController.view.translatesAutoresizingMaskIntoConstraints = false
        addChild(hostingController)
        container.addSubview(hostingController.view)
        NSLayoutConstraint.activate([
            hostingController.view.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            hostingController.view.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            hostingController.view.topAnchor.constraint(equalTo: container.topAnchor),
            hostingController.view.bottomAnchor.constraint(equalTo: container.bottomAnchor),
        ])
        hostingController.didMove(toParent: self)
    }

    override var keyCommands: [UIKeyCommand]? {
        let command = UIKeyCommand(
            action: #selector(handleEscape(_:)),
            input: UIKeyCommand.inputEscape,
            modifierFlags: [],
            discoverabilityTitle: CompanionL10n.string(
                "action.cancel",
                fallback: "Cancel"
            )
        )
        command.wantsPriorityOverSystemBehavior = true
        return [command]
    }

    func update(content: AnyView, onEscape: @escaping () -> Void) {
        hostingController.rootView = content
        self.onEscape = onEscape
    }

    @objc private func handleEscape(_ command: UIKeyCommand) {
        onEscape()
    }
}
