import Foundation
import SwiftUI
import AhoiCloudKitSpike

enum BookmarkLibraryRoute: Hashable {
    case root(BookmarkRoot)
    case folder(BookmarkID)
}

struct BookmarkContainer {
    let root: BookmarkRoot
    let parentID: BookmarkID?
    let title: String
}

struct BookmarkLibraryProjection {
    private let bookmarks: [BookmarkRecord]
    private let byID: [BookmarkID: BookmarkRecord]

    init(_ bookmarks: [BookmarkRecord]) {
        self.bookmarks = bookmarks
        var indexed: [BookmarkID: BookmarkRecord] = [:]
        for bookmark in bookmarks where indexed[bookmark.id] == nil {
            indexed[bookmark.id] = bookmark
        }
        self.byID = indexed
    }

    func container(for route: BookmarkLibraryRoute) -> BookmarkContainer? {
        switch route {
        case .root(let root):
            return BookmarkContainer(root: root, parentID: nil, title: root.title)
        case .folder(let id):
            guard let folder = byID[id], folder.kind == .folder,
                  let root = root(of: folder) else { return nil }
            return BookmarkContainer(
                root: root,
                parentID: folder.id,
                title: folder.title.isEmpty
                    ? CompanionL10n.string(
                        "bookmark.untitled_folder",
                        fallback: "Untitled Folder"
                    )
                    : folder.title
            )
        }
    }

    func children(in container: BookmarkContainer) -> [BookmarkRecord] {
        bookmarks.filter { bookmark in
            if let parentID = container.parentID {
                return bookmark.parentID == parentID
            }
            return bookmark.parentID == nil && bookmark.rootKind == container.root
        }.sorted(by: ordered)
    }

    func folderTargets(for bookmark: BookmarkRecord) -> [BookmarkRecord] {
        bookmarks.filter { candidate in
            candidate.kind == .folder && candidate.id != bookmark.id &&
                !wouldCreateCycle(moving: bookmark.id, below: candidate.id)
        }.sorted { lhs, rhs in
            let left = pathLabel(for: lhs)
            let right = pathLabel(for: rhs)
            let order = left.localizedCaseInsensitiveCompare(right)
            return order == .orderedSame ? lhs.id < rhs.id : order == .orderedAscending
        }
    }

    func pathLabel(for folder: BookmarkRecord) -> String {
        var names: [String] = []
        var current: BookmarkRecord? = folder
        var seen = Set<BookmarkID>()
        var resolvedRoot: BookmarkRoot?
        while let item = current, seen.insert(item.id).inserted {
            names.append(item.title.isEmpty
                ? CompanionL10n.string(
                    "bookmark.untitled_folder",
                    fallback: "Untitled Folder"
                )
                : item.title)
            if let root = item.rootKind {
                resolvedRoot = root
                break
            }
            current = item.parentID.flatMap { byID[$0] }
        }
        let path = names.reversed().joined(separator: " › ")
        guard let resolvedRoot else { return path }
        return "\(resolvedRoot.title) › \(path)"
    }

    private func root(of bookmark: BookmarkRecord) -> BookmarkRoot? {
        var current: BookmarkRecord? = bookmark
        var seen = Set<BookmarkID>()
        while let item = current, seen.insert(item.id).inserted {
            if let root = item.rootKind { return root }
            current = item.parentID.flatMap { byID[$0] }
        }
        return nil
    }

    private func wouldCreateCycle(moving id: BookmarkID, below folderID: BookmarkID) -> Bool {
        var currentID: BookmarkID? = folderID
        var seen = Set<BookmarkID>()
        while let candidateID = currentID, seen.insert(candidateID).inserted {
            if candidateID == id { return true }
            currentID = byID[candidateID]?.parentID
        }
        return false
    }

    private func ordered(_ lhs: BookmarkRecord, _ rhs: BookmarkRecord) -> Bool {
        if lhs.sortKey != rhs.sortKey { return lhs.sortKey < rhs.sortKey }
        return lhs.id < rhs.id
    }
}

struct BookmarkEditorState: Identifiable {
    let id = UUID()
    let bookmarkID: BookmarkID?
    let kind: BookmarkKind
    let rootKind: BookmarkRoot?
    let parentID: BookmarkID?
    let title: String
    let url: String

    static func creating(
        kind: BookmarkKind,
        in container: BookmarkContainer,
        title: String = "",
        url: String = ""
    ) -> Self {
        Self(
            bookmarkID: nil,
            kind: kind,
            rootKind: container.parentID == nil ? container.root : nil,
            parentID: container.parentID,
            title: title,
            url: url
        )
    }

    static func editing(_ bookmark: BookmarkRecord) -> Self {
        Self(
            bookmarkID: bookmark.id,
            kind: bookmark.kind,
            rootKind: bookmark.rootKind,
            parentID: bookmark.parentID,
            title: bookmark.title,
            url: bookmark.url
        )
    }
}

struct BookmarkEditorView: View {
    let state: BookmarkEditorState
    let onSave: @MainActor (String, String) async -> String?
    @Environment(\.dismiss) private var dismiss
    @State private var title: String
    @State private var url: String
    @State private var isSaving = false
    @State private var errorMessage: String?

    init(
        state: BookmarkEditorState,
        onSave: @escaping @MainActor (String, String) async -> String?
    ) {
        self.state = state
        self.onSave = onSave
        _title = State(initialValue: state.title)
        _url = State(initialValue: state.url)
    }

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField(
                        CompanionL10n.string(
                            "bookmark.editor.title",
                            fallback: "Title"
                        ),
                        text: $title
                    )
                    .accessibilityIdentifier("bookmark.editor.title")
                    if state.kind == .url {
                        TextField(
                            CompanionL10n.string(
                                "bookmark.editor.url",
                                fallback: "Address"
                            ),
                            text: $url
                        )
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .keyboardType(.URL)
                        .accessibilityIdentifier("bookmark.editor.url")
                    }
                } footer: {
                    if state.kind == .url {
                        Text(CompanionL10n.string(
                            "bookmark.editor.url_hint",
                            fallback: "Web addresses open on this device. Other native addresses remain saved as metadata."
                        ))
                    }
                }
                if let errorMessage {
                    Section {
                        Label(errorMessage, systemImage: "exclamationmark.triangle.fill")
                            .foregroundStyle(.red)
                    }
                }
            }
            .navigationTitle(editorTitle)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(CompanionL10n.string("action.cancel", fallback: "Cancel")) {
                        dismiss()
                    }
                    .disabled(isSaving)
                    .accessibilityIdentifier("bookmark.editor.cancel")
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(CompanionL10n.string("action.save", fallback: "Save")) {
                        save()
                    }
                    .disabled(!canSave || isSaving)
                    .accessibilityIdentifier("bookmark.editor.save")
                }
            }
        }
        .interactiveDismissDisabled(isSaving)
    }

    private var editorTitle: String {
        if state.bookmarkID != nil {
            return state.kind == .folder
                ? CompanionL10n.string("bookmark.editor.edit_folder", fallback: "Edit Folder")
                : CompanionL10n.string("bookmark.editor.edit_bookmark", fallback: "Edit Bookmark")
        }
        return state.kind == .folder
            ? CompanionL10n.string("bookmark.editor.add_folder", fallback: "New Folder")
            : CompanionL10n.string("bookmark.editor.add_bookmark", fallback: "New Bookmark")
    }

    private var canSave: Bool {
        guard title.utf8.count <= 65_536, !title.contains("\0") else { return false }
        guard state.kind == .url else { return true }
        guard !url.isEmpty, url.utf8.count <= 131_072, !url.contains("\0"),
              let parsed = URL(string: url), parsed.scheme != nil,
              let components = URLComponents(string: url),
              let scheme = components.scheme,
              components.user == nil, components.password == nil else { return false }
        if scheme.caseInsensitiveCompare("http") == .orderedSame ||
            scheme.caseInsensitiveCompare("https") == .orderedSame {
            return components.host?.isEmpty == false
        }
        return true
    }

    private func save() {
        guard canSave, !isSaving else { return }
        isSaving = true
        errorMessage = nil
        Task { @MainActor in
            if let error = await onSave(title, url) {
                errorMessage = error
                isSaving = false
            } else {
                dismiss()
            }
        }
    }
}

extension BookmarkRoot {
    var identifier: String {
        switch self {
        case .bar: "bar"
        case .other: "other"
        case .mobile: "mobile"
        }
    }

    var title: String {
        switch self {
        case .bar:
            CompanionL10n.string("bookmark.root.bar", fallback: "Bookmarks Bar")
        case .other:
            CompanionL10n.string("bookmark.root.other", fallback: "Other Bookmarks")
        case .mobile:
            CompanionL10n.string("bookmark.root.mobile", fallback: "Mobile Bookmarks")
        }
    }

    var systemImage: String {
        switch self {
        case .bar: "menubar.rectangle"
        case .other: "bookmark"
        case .mobile: "iphone"
        }
    }
}
