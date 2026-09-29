import Foundation
import SwiftUI
import AhoiCloudKitSpike

public struct BookmarkLibraryView: View {
    @ObservedObject private var model: CompanionAppModel
    @Environment(\.dismiss) private var dismiss
    private let openURL: OpenURLAction
    private let initialTitle: String?
    private let initialURL: String?

    @State private var path: [BookmarkLibraryRoute] = []
    @State private var editor: BookmarkEditorState?
    @State private var pendingDeletion: BookmarkRecord?
    @State private var unsupportedBookmark: BookmarkRecord?
    @State private var didPresentInitialEditor = false
    @State private var isEnablingSync = false

    public init(
        model: CompanionAppModel,
        openURL: OpenURLAction,
        initialTitle: String? = nil,
        initialURL: String? = nil
    ) {
        self.model = model
        self.openURL = openURL
        self.initialTitle = initialTitle
        self.initialURL = initialURL
    }

    public var body: some View {
        NavigationStack(path: $path) {
            rootList
                .navigationDestination(for: BookmarkLibraryRoute.self) { route in
                    containerView(route)
                }
        }
        .safeAreaInset(edge: .top) {
            if let message = model.loadError {
                CompanionOperationErrorBanner(
                    message: message,
                    dismiss: model.dismissLoadError
                )
            }
        }
        .accessibilityIdentifier("bookmark.library.root")
        .sheet(item: $editor) { state in
            BookmarkEditorView(state: state) { title, url in
                await saveEditor(state, title: title, url: url)
            }
        }
        .confirmationDialog(
            deletionTitle,
            isPresented: Binding(
                get: { pendingDeletion != nil },
                set: { if !$0 { pendingDeletion = nil } }
            ),
            titleVisibility: .visible
        ) {
            Button(
                CompanionL10n.string("action.delete", fallback: "Delete"),
                role: .destructive
            ) {
                guard let bookmark = pendingDeletion else { return }
                pendingDeletion = nil
                Task { await model.deleteBookmark(bookmark.id) }
            }
            .accessibilityIdentifier("bookmark.delete.confirm")
            Button(
                CompanionL10n.string("action.cancel", fallback: "Cancel"),
                role: .cancel
            ) {
                pendingDeletion = nil
            }
            .accessibilityIdentifier("bookmark.delete.cancel")
        } message: {
            Text(CompanionL10n.string(
                "bookmark.delete.message",
                fallback: "The bookmark and any contained items will be removed from enabled devices."
            ))
        }
        .alert(
            CompanionL10n.string(
                "bookmark.unsupported.title",
                fallback: "This address cannot be opened here"
            ),
            isPresented: Binding(
                get: { unsupportedBookmark != nil },
                set: { if !$0 { unsupportedBookmark = nil } }
            ),
            presenting: unsupportedBookmark
        ) { _ in
            Button(CompanionL10n.string("action.ok", fallback: "OK")) {
                unsupportedBookmark = nil
            }
        } message: { _ in
            Text(CompanionL10n.string(
                "bookmark.unsupported.message",
                fallback: "The address remains saved as bookmark metadata, but iPhone and iPad open only web addresses here."
            ))
        }
        .task { presentInitialBookmarkIfNeeded() }
        .accessibilityAction(.escape) { dismiss() }
    }

    private var rootList: some View {
        List {
            bookmarkSyncSection
            Section(CompanionL10n.string("bookmark.roots", fallback: "Bookmarks")) {
                ForEach(BookmarkRoot.allCases, id: \.rawValue) { root in
                    NavigationLink(value: BookmarkLibraryRoute.root(root)) {
                        Label(root.title, systemImage: root.systemImage)
                            .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                    }
                    .accessibilityIdentifier("bookmark.root.\(root.identifier)")
                }
            }
        }
        .listStyle(.insetGrouped)
        .navigationTitle(CompanionL10n.string(
            "bookmark.library.title",
            fallback: "Bookmarks"
        ))
        .toolbar { doneToolbar }
    }

    @ViewBuilder
    private var bookmarkSyncSection: some View {
        if !model.isBookmarkSyncEnabled {
            Section {
                Button {
                    Task { @MainActor in
                        isEnablingSync = true
                        defer { isEnablingSync = false }
                        await model.enableBookmarkSync()
                    }
                } label: {
                    Label(
                        CompanionL10n.string(
                            "bookmark.sync.enable",
                            fallback: "Enable Bookmark Sync"
                        ),
                        systemImage: "arrow.triangle.2.circlepath"
                    )
                    .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                }
                .disabled(!model.isSyncConfigured || isEnablingSync)
                .accessibilityIdentifier("bookmark.sync.enable")
            } footer: {
                if model.isSyncConfigured {
                    Text(CompanionL10n.string(
                        "bookmark.sync.enable.message",
                        fallback: "Enable this category once to share bookmarks through Ahoi Sync."
                    ))
                } else {
                    Text(CompanionL10n.string(
                        "bookmark.sync.prerequisite",
                        fallback: "Configure and enable Ahoi Sync in Settings before enabling this category."
                    ))
                }
            }
        } else if !model.isSyncConfigured {
            Section {
                Label(
                    CompanionL10n.string(
                        "bookmark.sync.local_only",
                        fallback: "Bookmarks remain available locally while Ahoi Sync is off."
                    ),
                    systemImage: "iphone"
                )
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
            }
        }
    }

    @ViewBuilder
    private func containerView(_ route: BookmarkLibraryRoute) -> some View {
        let projection = BookmarkLibraryProjection(model.snapshot.visibleBookmarks)
        if let container = projection.container(for: route) {
            let items = projection.children(in: container)
            List {
                if items.isEmpty {
                    ContentUnavailableView(
                        CompanionL10n.string(
                            "bookmark.empty.title",
                            fallback: "No bookmarks"
                        ),
                        systemImage: "bookmark",
                        description: Text(CompanionL10n.string(
                            "bookmark.empty.message",
                            fallback: "Add a bookmark or folder here."
                        ))
                    )
                } else {
                    ForEach(items) { bookmark in
                        bookmarkRow(bookmark, siblings: items, projection: projection)
                    }
                }
            }
            .listStyle(.insetGrouped)
            .navigationTitle(container.title)
            .toolbar {
                doneToolbar
                ToolbarItem(placement: .primaryAction) {
                    Menu {
                        Button {
                            editor = .creating(kind: .url, in: container)
                        } label: {
                            Label(
                                CompanionL10n.string(
                                    "bookmark.add",
                                    fallback: "Add Bookmark"
                                ),
                                systemImage: "bookmark"
                            )
                        }
                        .accessibilityIdentifier("bookmark.add")
                        Button {
                            editor = .creating(kind: .folder, in: container)
                        } label: {
                            Label(
                                CompanionL10n.string(
                                    "bookmark.add_folder",
                                    fallback: "Add Folder"
                                ),
                                systemImage: "folder.badge.plus"
                            )
                        }
                        .accessibilityIdentifier("bookmark.add-folder")
                    } label: {
                        Image(systemName: "plus")
                    }
                    .accessibilityLabel(CompanionL10n.string(
                        "bookmark.add_menu",
                        fallback: "Add"
                    ))
                    .accessibilityIdentifier("bookmark.add-menu")
                }
            }
        } else {
            ContentUnavailableView(
                CompanionL10n.string(
                    "bookmark.folder.unavailable",
                    fallback: "Folder unavailable"
                ),
                systemImage: "folder.badge.questionmark"
            )
            .toolbar { doneToolbar }
        }
    }

    @ToolbarContentBuilder
    private var doneToolbar: some ToolbarContent {
        ToolbarItem(placement: .cancellationAction) {
            Button(CompanionL10n.string("action.done", fallback: "Done")) {
                dismiss()
            }
            .accessibilityIdentifier("bookmark.done")
        }
    }

    @ViewBuilder
    private func bookmarkRow(
        _ bookmark: BookmarkRecord,
        siblings: [BookmarkRecord],
        projection: BookmarkLibraryProjection
    ) -> some View {
        Group {
            if bookmark.kind == .folder {
                NavigationLink(value: BookmarkLibraryRoute.folder(bookmark.id)) {
                    bookmarkLabel(bookmark)
                }
            } else {
                Button { open(bookmark) } label: {
                    bookmarkLabel(bookmark)
                }
                .buttonStyle(.plain)
            }
        }
        .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
        .contentShape(Rectangle())
        .accessibilityIdentifier("bookmark.row.\(bookmark.id.rawValue.uuidString.lowercased())")
        .contextMenu {
            Button {
                editor = .editing(bookmark)
            } label: {
                Label(
                    CompanionL10n.string("bookmark.edit", fallback: "Edit"),
                    systemImage: "pencil"
                )
            }
            .accessibilityIdentifier(actionID("edit", bookmark))
            moveMenu(bookmark, projection: projection)
            Button {
                reorderUp(bookmark, siblings: siblings)
            } label: {
                Label(
                    CompanionL10n.string("bookmark.move_up", fallback: "Move Up"),
                    systemImage: "arrow.up"
                )
            }
            .disabled(!canMoveUp(bookmark, siblings: siblings))
            .accessibilityIdentifier(actionID("move-up", bookmark))
            Button {
                reorderDown(bookmark, siblings: siblings)
            } label: {
                Label(
                    CompanionL10n.string("bookmark.move_down", fallback: "Move Down"),
                    systemImage: "arrow.down"
                )
            }
            .disabled(!canMoveDown(bookmark, siblings: siblings))
            .accessibilityIdentifier(actionID("move-down", bookmark))
            Button(role: .destructive) {
                pendingDeletion = bookmark
            } label: {
                Label(
                    CompanionL10n.string("action.delete", fallback: "Delete"),
                    systemImage: "trash"
                )
            }
            .accessibilityIdentifier(actionID("delete", bookmark))
        }
    }

    private func bookmarkLabel(_ bookmark: BookmarkRecord) -> some View {
        HStack(spacing: 12) {
            Group {
                if bookmark.kind == .folder {
                    Image(systemName: "folder.fill")
                        .foregroundStyle(.secondary)
                } else {
                    Image(systemName: "bookmark.fill")
                        .foregroundStyle(.tint)
                }
            }
                .frame(width: 24)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 3) {
                Text(displayTitle(bookmark))
                    .foregroundStyle(.primary)
                    .lineLimit(1)
                if bookmark.kind == .url {
                    Text(bookmark.url)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            }
            Spacer(minLength: 8)
        }
    }

    private func moveMenu(
        _ bookmark: BookmarkRecord,
        projection: BookmarkLibraryProjection
    ) -> some View {
        Menu {
            Section(CompanionL10n.string("bookmark.roots", fallback: "Bookmarks")) {
                ForEach(BookmarkRoot.allCases, id: \.rawValue) { root in
                    Button(root.title) {
                        Task {
                            await model.moveBookmark(
                                bookmark.id,
                                rootKind: root,
                                parentID: nil
                            )
                        }
                    }
                    .disabled(bookmark.rootKind == root && bookmark.parentID == nil)
                    .accessibilityIdentifier(
                        "\(actionID("move", bookmark)).root.\(root.identifier)"
                    )
                }
            }
            let folders = projection.folderTargets(for: bookmark)
            if !folders.isEmpty {
                Section(CompanionL10n.string(
                    "bookmark.folders",
                    fallback: "Folders"
                )) {
                    ForEach(folders) { folder in
                        Button(projection.pathLabel(for: folder)) {
                            Task {
                                await model.moveBookmark(
                                    bookmark.id,
                                    rootKind: nil,
                                    parentID: folder.id
                                )
                            }
                        }
                        .disabled(bookmark.parentID == folder.id)
                        .accessibilityIdentifier(
                            "\(actionID("move", bookmark)).folder.\(folder.id.rawValue.uuidString.lowercased())"
                        )
                    }
                }
            }
        } label: {
            Label(
                CompanionL10n.string("bookmark.move", fallback: "Move To"),
                systemImage: "folder"
            )
        }
        .accessibilityIdentifier(actionID("move", bookmark))
    }

    private func open(_ bookmark: BookmarkRecord) {
        guard let candidate = URL(string: bookmark.url),
              let safeURL = try? MobileBrowserInputRouter.validateWebURL(candidate) else {
            unsupportedBookmark = bookmark
            return
        }
        openURL(safeURL)
        dismiss()
    }

    private func reorderUp(_ bookmark: BookmarkRecord, siblings: [BookmarkRecord]) {
        guard let index = siblings.firstIndex(where: { $0.id == bookmark.id }),
              index > siblings.startIndex else { return }
        let successor = siblings[siblings.index(before: index)].id
        Task { await model.reorderBookmark(bookmark.id, before: successor) }
    }

    private func reorderDown(_ bookmark: BookmarkRecord, siblings: [BookmarkRecord]) {
        guard let index = siblings.firstIndex(where: { $0.id == bookmark.id }),
              index < siblings.index(before: siblings.endIndex) else { return }
        let nextIndex = siblings.index(after: index)
        let successorIndex = siblings.index(after: nextIndex)
        let successor = successorIndex < siblings.endIndex ? siblings[successorIndex].id : nil
        Task { await model.reorderBookmark(bookmark.id, before: successor) }
    }

    private func canMoveUp(_ bookmark: BookmarkRecord, siblings: [BookmarkRecord]) -> Bool {
        siblings.firstIndex(where: { $0.id == bookmark.id }).map { $0 > 0 } ?? false
    }

    private func canMoveDown(_ bookmark: BookmarkRecord, siblings: [BookmarkRecord]) -> Bool {
        siblings.firstIndex(where: { $0.id == bookmark.id }).map {
            $0 + 1 < siblings.count
        } ?? false
    }

    private func saveEditor(
        _ state: BookmarkEditorState,
        title: String,
        url: String
    ) async -> String? {
        if let bookmarkID = state.bookmarkID {
            let saved = await model.updateBookmark(
                bookmarkID,
                title: title,
                url: state.kind == .url ? url : nil
            )
            return saved ? nil : (model.loadError ?? CompanionL10n.string(
                "bookmark.editor.save_failed", fallback: "The bookmark could not be saved."
            ))
        }
        let created = await model.createBookmark(
            kind: state.kind,
            rootKind: state.rootKind,
            parentID: state.parentID,
            title: title,
            url: state.kind == .url ? url : ""
        )
        guard created != nil else {
            return model.loadError ?? CompanionL10n.string(
                "bookmark.editor.save_failed",
                fallback: "The bookmark could not be saved."
            )
        }
        return nil
    }

    private func presentInitialBookmarkIfNeeded() {
        guard !didPresentInitialEditor else { return }
        didPresentInitialEditor = true
        guard let initialURL, !initialURL.isEmpty else { return }
        editor = .creating(
            kind: .url,
            in: BookmarkContainer(
                root: .mobile,
                parentID: nil,
                title: BookmarkRoot.mobile.title
            ),
            title: initialTitle ?? "",
            url: initialURL
        )
    }

    private var deletionTitle: String {
        guard let pendingDeletion else { return "" }
        return CompanionL10n.format(
            "bookmark.delete.confirmation",
            fallback: "Delete %@?",
            String(displayTitle(pendingDeletion).prefix(160))
        )
    }

    private func displayTitle(_ bookmark: BookmarkRecord) -> String {
        if !bookmark.title.isEmpty { return bookmark.title }
        return bookmark.kind == .folder
            ? CompanionL10n.string("bookmark.untitled_folder", fallback: "Untitled Folder")
            : bookmark.url
    }

    private func actionID(_ action: String, _ bookmark: BookmarkRecord) -> String {
        "bookmark.action.\(action).\(bookmark.id.rawValue.uuidString.lowercased())"
    }
}
