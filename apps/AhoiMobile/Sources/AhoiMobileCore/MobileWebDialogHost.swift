import SwiftUI
import UniformTypeIdentifiers

struct MobileWebDialogHost: View {
    @ObservedObject var presenter: MobileWebDialogPresenter
    let onPresentationRequested: () -> Void
    @State private var promptText = ""
    @State private var fileImporterPresented = false
    @State private var activeFileInputRequest: MobileFileInputRequest?

    var body: some View {
        Color.clear
            .frame(width: 0, height: 0)
            .alert(
                presenter.pendingJavaScriptDialog?.origin ?? "AhoiBrowser",
                isPresented: javaScriptDialogPresented,
                presenting: presenter.pendingJavaScriptDialog
            ) { request in
                switch request.kind {
                case .alert:
                    Button(CompanionL10n.string("action.ok", fallback: "OK")) {
                        presenter.acknowledgeAlert(requestID: request.id)
                    }
                    .accessibilityIdentifier("browser.dialog.accept")
                case .confirm:
                    Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {
                        presenter.respondToConfirmation(
                            requestID: request.id,
                            confirmed: false
                        )
                    }
                    .accessibilityIdentifier("browser.dialog.cancel")
                    Button(CompanionL10n.string("action.ok", fallback: "OK")) {
                        presenter.respondToConfirmation(
                            requestID: request.id,
                            confirmed: true
                        )
                    }
                    .accessibilityIdentifier("browser.dialog.accept")
                case .prompt(_, let defaultText):
                    TextField(
                        CompanionL10n.string(
                            "browser.dialog.response",
                            fallback: "Response"
                        ),
                        text: $promptText
                    )
                    .onAppear { promptText = defaultText ?? "" }
                    Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {
                        presenter.cancel(requestID: request.id)
                    }
                    .accessibilityIdentifier("browser.dialog.cancel")
                    Button(CompanionL10n.string("action.ok", fallback: "OK")) {
                        presenter.respondToPrompt(
                            requestID: request.id,
                            submittedText: promptText
                        )
                    }
                    .accessibilityIdentifier("browser.dialog.accept")
                }
            } message: { request in
                Text(request.kind.message)
            }
            .alert(
                fileInputTitle,
                isPresented: fileInputConfirmationPresented,
                presenting: presenter.pendingFileInput
            ) { request in
                Button(CompanionL10n.string(
                    "browser.file_input.choose",
                    fallback: "Choose Files"
                )) {
                    activeFileInputRequest = request
                    fileImporterPresented = true
                }
                .accessibilityIdentifier("browser.file_input.choose")
                Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {
                    presenter.cancel(requestID: request.id)
                }
                .accessibilityIdentifier("browser.file_input.cancel")
            } message: { request in
                Text(CompanionL10n.format(
                    "browser.file_input.message",
                    fallback: "%@ wants to select %@ from this device.",
                    request.origin,
                    request.allowsDirectories
                        ? CompanionL10n.string("browser.file_input.folders", fallback: "folders")
                        : CompanionL10n.string("browser.file_input.files", fallback: "files")
                ))
            }
            .fileImporter(
                isPresented: $fileImporterPresented,
                allowedContentTypes: allowedContentTypes,
                allowsMultipleSelection: activeFileInputRequest?.allowsMultipleSelection == true
            ) { result in
                guard let request = activeFileInputRequest else { return }
                activeFileInputRequest = nil
                switch result {
                case .success(let urls):
                    presenter.selectFiles(requestID: request.id, urls: urls)
                case .failure:
                    presenter.cancel(requestID: request.id)
                }
            }
            .onChange(of: chromeResetContext) { previous, current in
                if current.requiresExpansion(comparedTo: previous) {
                    onPresentationRequested()
                }
            }
            .onChange(of: presenter.pendingFileInput?.id) { _, requestID in
                if let activeFileInputRequest,
                   requestID != activeFileInputRequest.id {
                    fileImporterPresented = false
                    self.activeFileInputRequest = nil
                } else if requestID == nil {
                    fileImporterPresented = false
                    self.activeFileInputRequest = nil
                }
            }
            .onDisappear {
                fileImporterPresented = false
                activeFileInputRequest = nil
            }
    }

    private var javaScriptDialogPresented: Binding<Bool> {
        Binding(
            get: { presenter.pendingJavaScriptDialog != nil },
            // Every button resolves the exact request it was rendered for.
            // Reading the presenter's *current* request from a stale dismissal
            // callback could otherwise cancel the next page-owned dialog.
            set: { _ in }
        )
    }

    private var chromeResetContext: MobileChromePresentationResetContext {
        MobileChromePresentationResetContext(
            javaScriptDialogID: presenter.pendingJavaScriptDialog?.id,
            fileInputRequestID: presenter.pendingFileInput?.id
        )
    }

    private var fileInputConfirmationPresented: Binding<Bool> {
        Binding(
            get: {
                presenter.pendingFileInput != nil &&
                    activeFileInputRequest == nil && !fileImporterPresented
            },
            set: { presented in
                guard !presented, !fileImporterPresented,
                      let request = presenter.pendingFileInput else { return }
                presenter.cancel(requestID: request.id)
            }
        )
    }

    private var fileInputTitle: String {
        CompanionL10n.string(
            "browser.file_input.title",
            fallback: "Website File Access"
        )
    }

    private var allowedContentTypes: [UTType] {
        activeFileInputRequest?.allowsDirectories == true ? [.folder] : [.item]
    }
}
