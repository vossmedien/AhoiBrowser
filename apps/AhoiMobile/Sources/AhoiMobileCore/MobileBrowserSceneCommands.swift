import SwiftUI
import UIKit
import OSLog

private enum MobileBrowserCommandDiagnostics {
    static let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "app.ahoibrowser.AhoiBrowser",
        category: "MobileBrowserCommands"
    )
}

@MainActor
public final class MobileBrowserCommandRouter: ObservableObject {
    @Published public private(set) var actions: MobileBrowserCommandActions?
    private var ownerID: UUID?

    public init() {}

    func register(_ actions: MobileBrowserCommandActions, ownerID: UUID) {
        self.actions = actions
        self.ownerID = ownerID
#if DEBUG
        MobileBrowserCommandDiagnostics.logger.notice(
            "registered owner=\(ownerID.uuidString, privacy: .public) sidebar=\(actions.canToggleSidebar)"
        )
#endif
    }

    func clear(ownerID: UUID) {
        guard self.ownerID == ownerID else { return }
        actions = nil
        self.ownerID = nil
#if DEBUG
        MobileBrowserCommandDiagnostics.logger.notice(
            "cleared owner=\(ownerID.uuidString, privacy: .public)"
        )
#endif
    }
}

private struct MobileBrowserCommandRouterEnvironmentKey: EnvironmentKey {
    static let defaultValue: MobileBrowserCommandRouter? = nil
}

public extension EnvironmentValues {
    var mobileBrowserCommandRouter: MobileBrowserCommandRouter? {
        get { self[MobileBrowserCommandRouterEnvironmentKey.self] }
        set { self[MobileBrowserCommandRouterEnvironmentKey.self] = newValue }
    }
}

@MainActor
public struct MobileBrowserCommandActions {
    public let tabCount: Int
    public let canReopenClosedTab: Bool
    public let canSwitchWorkspace: Bool
    public let canToggleSidebar: Bool
    public let canDismissPresentation: Bool
    let newTab: () -> Void
    let newPrivateTab: () -> Void
    let reopenClosedTab: () -> Void
    let closeSelectedTab: () -> Void
    let presentAddress: () -> Void
    let presentTabs: () -> Void
    let toggleSidebar: () -> Void
    let switchWorkspace: (Int) -> Void
    let switchTab: (Int) -> Void
    let selectNumberedTab: (Int) -> Void
    let dismissPresentation: () -> Void

    public init(
        tabCount: Int,
        canReopenClosedTab: Bool,
        canSwitchWorkspace: Bool,
        canToggleSidebar: Bool,
        canDismissPresentation: Bool = false,
        newTab: @escaping () -> Void,
        newPrivateTab: @escaping () -> Void,
        reopenClosedTab: @escaping () -> Void,
        closeSelectedTab: @escaping () -> Void,
        presentAddress: @escaping () -> Void,
        presentTabs: @escaping () -> Void,
        toggleSidebar: @escaping () -> Void,
        switchWorkspace: @escaping (Int) -> Void,
        switchTab: @escaping (Int) -> Void,
        selectNumberedTab: @escaping (Int) -> Void,
        dismissPresentation: @escaping () -> Void = {}
    ) {
        self.tabCount = max(0, tabCount)
        self.canReopenClosedTab = canReopenClosedTab
        self.canSwitchWorkspace = canSwitchWorkspace
        self.canToggleSidebar = canToggleSidebar
        self.canDismissPresentation = canDismissPresentation
        self.newTab = newTab
        self.newPrivateTab = newPrivateTab
        self.reopenClosedTab = reopenClosedTab
        self.closeSelectedTab = closeSelectedTab
        self.presentAddress = presentAddress
        self.presentTabs = presentTabs
        self.toggleSidebar = toggleSidebar
        self.switchWorkspace = switchWorkspace
        self.switchTab = switchTab
        self.selectNumberedTab = selectNumberedTab
        self.dismissPresentation = dismissPresentation
    }

    func selectTab(number: Int) {
        guard MobileBrowserCommandTabPolicy.targetIndex(
            number: number,
            tabCount: tabCount
        ) != nil else { return }
        selectNumberedTab(number)
    }
}

private struct MobileBrowserCommandAvailability: Equatable {
    let tabCount: Int
    let canReopenClosedTab: Bool
    let canSwitchWorkspace: Bool
    let canToggleSidebar: Bool
    let canDismissPresentation: Bool

    init(_ actions: MobileBrowserCommandActions) {
        tabCount = actions.tabCount
        canReopenClosedTab = actions.canReopenClosedTab
        canSwitchWorkspace = actions.canSwitchWorkspace
        canToggleSidebar = actions.canToggleSidebar
        canDismissPresentation = actions.canDismissPresentation
    }
}

@MainActor
private struct MobileBrowserCommandRegistration: ViewModifier {
    @State private var ownerID = UUID()
    let actions: MobileBrowserCommandActions
    let router: MobileBrowserCommandRouter?

    private var availability: MobileBrowserCommandAvailability {
        MobileBrowserCommandAvailability(actions)
    }

    func body(content: Content) -> some View {
        content
            .onAppear { router?.register(actions, ownerID: ownerID) }
            .onChange(of: availability) { _, _ in
                router?.register(actions, ownerID: ownerID)
            }
            .onDisappear { router?.clear(ownerID: ownerID) }
    }
}

extension View {
    @MainActor
    func mobileBrowserCommandRegistration(
        _ actions: MobileBrowserCommandActions,
        router: MobileBrowserCommandRouter?
    ) -> some View {
        modifier(MobileBrowserCommandRegistration(actions: actions, router: router))
    }
}

/// Supplies the otherwise empty Focus Voyage with a real UIKit responder so
/// scene keyboard commands remain available before a web view or text field
/// exists. It only claims an unowned responder chain and therefore yields to
/// editable controls, dialogs and WKWebView as soon as they request focus.
struct MobileBrowserKeyboardFocusAnchor: UIViewControllerRepresentable {
    let router: MobileBrowserCommandRouter?

    func makeUIViewController(
        context: Context
    ) -> MobileBrowserKeyboardCommandController {
        let controller = MobileBrowserKeyboardCommandController()
        controller.router = router
        return controller
    }

    func updateUIViewController(
        _ uiViewController: MobileBrowserKeyboardCommandController,
        context: Context
    ) {
        uiViewController.router = router
        uiViewController.setNeedsUpdateOfKeyCommands()
        uiViewController.claimUnownedResponderChain()
    }
}

final class MobileBrowserKeyboardCommandController: UIViewController {
    var router: MobileBrowserCommandRouter?

    override var canBecomeFirstResponder: Bool { true }

    override func loadView() {
        let view = UIView()
        view.backgroundColor = .clear
        view.accessibilityElementsHidden = true
        view.isAccessibilityElement = false
        self.view = view
    }

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var unhandledPresses = presses
        for press in presses {
            guard let key = press.key,
                  handleFallbackPress(
                      key,
                      modifiers: event?.modifierFlags ?? key.modifierFlags
                  ) else { continue }
            unhandledPresses.remove(press)
        }
        guard !unhandledPresses.isEmpty else { return }
        super.pressesBegan(unhandledPresses, with: event)
    }

    /// SwiftUI scene commands provide the menu and discoverability surface.
    /// These narrow responder commands cover the empty Focus Voyage where
    /// iPadOS does not project scene shortcuts into the responder chain yet.
    override var keyCommands: [UIKeyCommand]? {
        guard let actions = router?.actions else { return [] }
        var commands = [
            fallbackKeyCommand(
                input: "l",
                modifiers: .command,
                action: #selector(presentAddress(_:)),
                title: CompanionL10n.string(
                    "browser.address.accessibility",
                    fallback: "Address and search"
                )
            ),
        ]
        if actions.canToggleSidebar {
            commands.append(fallbackKeyCommand(
                input: "s",
                modifiers: [.command, .control],
                action: #selector(toggleSidebar(_:)),
                title: CompanionL10n.string(
                    "browser.sidebar.toggle",
                    fallback: "Toggle sidebar"
                )
            ))
        }
        if actions.canDismissPresentation {
            commands.append(fallbackKeyCommand(
                input: UIKeyCommand.inputEscape,
                modifiers: [],
                action: #selector(dismissPresentation(_:)),
                title: CompanionL10n.string("action.close", fallback: "Close")
            ))
        }
        if actions.canSwitchWorkspace {
            commands.append(contentsOf: [
                fallbackKeyCommand(
                    input: "[",
                    modifiers: [.command, .control],
                    action: #selector(previousWorkspace(_:)),
                    title: CompanionL10n.string(
                        "browser.workspace.previous",
                        fallback: "Previous workspace"
                    )
                ),
                fallbackKeyCommand(
                    input: "]",
                    modifiers: [.command, .control],
                    action: #selector(nextWorkspace(_:)),
                    title: CompanionL10n.string(
                        "browser.workspace.next",
                        fallback: "Next workspace"
                    )
                ),
            ])
        }
        return commands
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
#if DEBUG
        MobileBrowserCommandDiagnostics.logger.notice(
            "controller appeared window=\(self.view.window != nil) key=\(self.view.window?.isKeyWindow == true)"
        )
#endif
        observeCurrentWindowBecomingKey()
        claimUnownedResponderChain()
    }

    override func viewDidDisappear(_ animated: Bool) {
        NotificationCenter.default.removeObserver(self)
        super.viewDidDisappear(animated)
    }

    func claimUnownedResponderChain() {
        DispatchQueue.main.async { [weak self] in
            guard let self, let window = view.window else { return }
            let existingResponder = window.ahoiFirstResponder
#if DEBUG
            let existingName = existingResponder.map { String(describing: type(of: $0)) } ?? "none"
            MobileBrowserCommandDiagnostics.logger.notice(
                "anchor claim key=\(window.isKeyWindow) existing=\(existingName, privacy: .public)"
            )
#endif
            guard window.isKeyWindow,
                  existingResponder == nil,
                  !isFirstResponder else { return }
            let claimed = self.becomeFirstResponder()
#if DEBUG
            MobileBrowserCommandDiagnostics.logger.notice(
                "anchor claimed=\(claimed) active=\(self.isFirstResponder)"
            )
#endif
        }
    }

    private func observeCurrentWindowBecomingKey() {
        NotificationCenter.default.removeObserver(
            self,
            name: UIWindow.didBecomeKeyNotification,
            object: nil
        )
        guard let window = view.window else { return }
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(windowDidBecomeKey(_:)),
            name: UIWindow.didBecomeKeyNotification,
            object: window
        )
    }

    @objc private func windowDidBecomeKey(_ notification: Notification) {
#if DEBUG
        MobileBrowserCommandDiagnostics.logger.notice("window became key")
#endif
        claimUnownedResponderChain()
    }

    private func fallbackKeyCommand(
        input: String,
        modifiers: UIKeyModifierFlags,
        action: Selector,
        title: String
    ) -> UIKeyCommand {
        let command = UIKeyCommand(
            action: action,
            input: input,
            modifierFlags: modifiers,
            discoverabilityTitle: title
        )
        command.wantsPriorityOverSystemBehavior = true
        return command
    }

    private func handleFallbackPress(
        _ key: UIKey,
        modifiers rawModifiers: UIKeyModifierFlags
    ) -> Bool {
        let modifiers = rawModifiers.intersection([
            .alternate,
            .command,
            .control,
            .shift,
        ])
        let input = key.charactersIgnoringModifiers.lowercased()
#if DEBUG
        MobileBrowserCommandDiagnostics.logger.notice(
            "press input=\(input, privacy: .public) eventModifiers=\(modifiers.rawValue) keyModifiers=\(key.modifierFlags.rawValue)"
        )
#endif
        switch (input, modifiers) {
        case (UIKeyCommand.inputEscape, []):
            guard router?.actions?.canDismissPresentation == true else { return false }
            router?.actions?.dismissPresentation()
        case ("l", .command):
            router?.actions?.presentAddress()
        case ("s", [.command, .control]):
            guard router?.actions?.canToggleSidebar == true else { return false }
            router?.actions?.toggleSidebar()
        case ("[", [.command, .control]):
            guard router?.actions?.canSwitchWorkspace == true else { return false }
            router?.actions?.switchWorkspace(-1)
        case ("]", [.command, .control]):
            guard router?.actions?.canSwitchWorkspace == true else { return false }
            router?.actions?.switchWorkspace(1)
        default:
            return false
        }
        return true
    }

    @objc private func presentAddress(_ command: UIKeyCommand) {
#if DEBUG
        MobileBrowserCommandDiagnostics.logger.notice("fallback address")
#endif
        router?.actions?.presentAddress()
    }

    override func toggleSidebar(_ sender: Any?) {
#if DEBUG
        MobileBrowserCommandDiagnostics.logger.notice("fallback sidebar")
#endif
        router?.actions?.toggleSidebar()
    }

    @objc private func previousWorkspace(_ command: UIKeyCommand) {
#if DEBUG
        MobileBrowserCommandDiagnostics.logger.notice("fallback previous workspace")
#endif
        router?.actions?.switchWorkspace(-1)
    }

    @objc private func nextWorkspace(_ command: UIKeyCommand) {
#if DEBUG
        MobileBrowserCommandDiagnostics.logger.notice("fallback next workspace")
#endif
        router?.actions?.switchWorkspace(1)
    }

    @objc private func dismissPresentation(_ command: UIKeyCommand) {
        router?.actions?.dismissPresentation()
    }
}

private extension UIView {
    var ahoiFirstResponder: UIView? {
        if isFirstResponder { return self }
        for subview in subviews {
            if let responder = subview.ahoiFirstResponder { return responder }
        }
        return nil
    }
}

enum MobileBrowserCommandTabPolicy {
    static func targetIndex(number: Int, tabCount: Int) -> Int? {
        guard (1...9).contains(number), tabCount > 0 else { return nil }
        let index = number == 9 ? tabCount - 1 : number - 1
        return index < tabCount ? index : nil
    }
}

@MainActor
public struct MobileBrowserSceneCommands: Commands {
    @ObservedObject private var router: MobileBrowserCommandRouter

    private var actions: MobileBrowserCommandActions? {
        router.actions
    }

    public init(router: MobileBrowserCommandRouter) {
        self.router = router
    }

    public var body: some Commands {
        CommandGroup(replacing: .newItem) {
            Button(CompanionL10n.string("browser.new_tab", fallback: "New tab")) {
                actions?.newTab()
            }
            .keyboardShortcut("t", modifiers: .command)
            Button(CompanionL10n.string(
                "browser.new_private_tab",
                fallback: "New private tab"
            )) {
                actions?.newPrivateTab()
            }
            .keyboardShortcut("n", modifiers: [.command, .shift])
            Button(CompanionL10n.string(
                "browser.undo_close_tab",
                fallback: "Reopen closed tab"
            )) {
                actions?.reopenClosedTab()
            }
            .keyboardShortcut("t", modifiers: [.command, .shift])
            .disabled(actions?.canReopenClosedTab != true)
        }
        CommandGroup(after: .newItem) {
            Button(CompanionL10n.string("browser.close_tab", fallback: "Close tab")) {
                actions?.closeSelectedTab()
            }
            .keyboardShortcut("w", modifiers: .command)
            .disabled(actions == nil)
        }
        CommandMenu(CompanionL10n.string("browser.tabs.title", fallback: "Tabs")) {
            Button(CompanionL10n.string(
                "browser.address.accessibility",
                fallback: "Address and search"
            )) {
#if DEBUG
                MobileBrowserCommandDiagnostics.logger.notice("command address")
#endif
                actions?.presentAddress()
            }
            .keyboardShortcut("l", modifiers: .command)
            Button(CompanionL10n.string("browser.tabs.title", fallback: "Show Tabs")) {
                actions?.presentTabs()
            }
            .keyboardShortcut("\\", modifiers: [.command, .shift])
            if actions?.canDismissPresentation == true {
                Button(CompanionL10n.string("action.close", fallback: "Close")) {
                    actions?.dismissPresentation()
                }
                .keyboardShortcut(.escape, modifiers: [])
            }
            Divider()
            Button(CompanionL10n.string(
                "browser.tabs.previous",
                fallback: "Previous Tab"
            )) {
                actions?.switchTab(-1)
            }
            .keyboardShortcut(.tab, modifiers: [.control, .shift])
            .disabled((actions?.tabCount ?? 0) < 2)
            Button(CompanionL10n.string(
                "browser.tabs.next",
                fallback: "Next Tab"
            )) {
                actions?.switchTab(1)
            }
            .keyboardShortcut(.tab, modifiers: .control)
            .disabled((actions?.tabCount ?? 0) < 2)
            Divider()
            ForEach(1...9, id: \.self) { number in
                numberedTabButton(number)
            }
        }
        CommandMenu(CompanionL10n.string("root.workspaces", fallback: "Workspaces")) {
            Button(CompanionL10n.string(
                "browser.workspace.previous",
                fallback: "Previous workspace"
            )) {
                actions?.switchWorkspace(-1)
            }
            .keyboardShortcut("[", modifiers: [.command, .control])
            .disabled(actions?.canSwitchWorkspace != true)
            Button(CompanionL10n.string(
                "browser.workspace.next",
                fallback: "Next workspace"
            )) {
                actions?.switchWorkspace(1)
            }
            .keyboardShortcut("]", modifiers: [.command, .control])
            .disabled(actions?.canSwitchWorkspace != true)
            Divider()
            Button(CompanionL10n.string(
                "browser.sidebar.toggle",
                fallback: "Toggle sidebar"
            )) {
#if DEBUG
                MobileBrowserCommandDiagnostics.logger.notice("command sidebar")
#endif
                actions?.toggleSidebar()
            }
            .keyboardShortcut("s", modifiers: [.command, .control])
            .disabled(actions?.canToggleSidebar != true)
        }
    }

    private func numberedTabButton(_ number: Int) -> some View {
        Button(CompanionL10n.format(
            "browser.tabs.select_numbered",
            fallback: "Select Tab %d",
            number
        )) {
            actions?.selectTab(number: number)
        }
        .keyboardShortcut(
            KeyEquivalent(Character(String(number))),
            modifiers: .command
        )
        .disabled(MobileBrowserCommandTabPolicy.targetIndex(
            number: number,
            tabCount: actions?.tabCount ?? 0
        ) == nil)
    }
}
