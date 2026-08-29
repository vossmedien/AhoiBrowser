import SwiftUI
import UniformTypeIdentifiers

struct MobileWebDialogHost: View {
    @ObservedObject var presenter: MobileWebDialogPresenter
    @State private var promptText = ""
    @State private var fileImporterPresented = false

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
                case .confirm:
                    Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {
                        presenter.respondToConfirmation(
                            requestID: request.id,
                            confirmed: false
                        )
                    }
                    Button(CompanionL10n.string("action.ok", fallback: "OK")) {
                        presenter.respondToConfirmation(
                            requestID: request.id,
                            confirmed: true
                        )
                    }
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
                    Button(CompanionL10n.string("action.ok", fallback: "OK")) {
                        presenter.respondToPrompt(
                            requestID: request.id,
                            submittedText: promptText
                        )
                    }
                }
            } message: { request in
                Text(request.kind.message)
            }
            .confirmationDialog(
                fileInputTitle,
                isPresented: fileInputConfirmationPresented,
                titleVisibility: .visible,
                presenting: presenter.pendingFileInput
            ) { request in
                Button(CompanionL10n.string(
                    "browser.file_input.choose",
                    fallback: "Choose Files"
                )) {
                    fileImporterPresented = true
                }
                Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {
                    presenter.cancel(requestID: request.id)
                }
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
                allowsMultipleSelection: presenter.pendingFileInput?.allowsMultipleSelection == true
            ) { result in
                guard let requestID = presenter.pendingFileInput?.id else { return }
                switch result {
                case .success(let urls):
                    presenter.selectFiles(requestID: requestID, urls: urls)
                case .failure:
                    presenter.cancel(requestID: requestID)
                }
            }
    }

    private var javaScriptDialogPresented: Binding<Bool> {
        Binding(
            get: { presenter.pendingJavaScriptDialog != nil },
            set: { presented in
                guard !presented, let request = presenter.pendingJavaScriptDialog else {
                    return
                }
                presenter.cancel(requestID: request.id)
            }
        )
    }

    private var fileInputConfirmationPresented: Binding<Bool> {
        Binding(
            get: {
                presenter.pendingFileInput != nil && !fileImporterPresented
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
        presenter.pendingFileInput?.allowsDirectories == true ? [.folder] : [.item]
    }
}
