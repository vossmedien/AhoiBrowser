import SwiftUI

/// Existing native picker plus one explicit Sync category. Remote application
/// updates AppStorage, not this user-action Binding, so it cannot reauthor itself.
struct CompanionBrowserSettingsSection: View {
    @ObservedObject var model: CompanionAppModel
    @AppStorage(MobileBrowserPreferences.searchEngineKey)
    private var searchEngineRawValue = MobileSearchEngine.duckDuckGo.rawValue

    var body: some View {
        Section {
            Picker(
                CompanionL10n.string("settings.search_engine.title", fallback: "Search engine"),
                selection: Binding(
                    get: { searchEngineRawValue },
                    set: { rawValue in
                        guard let engine = MobileSearchEngine(rawValue: rawValue) else { return }
                        model.setBrowserSearchEngine(engine)
                    }
                )
            ) {
                ForEach(MobileSearchEngine.allCases) { engine in
                    Text(engine.localizedName).tag(engine.rawValue)
                }
            }
            .accessibilityIdentifier("settings.search.engine")

            if model.defaults.object(forKey: MobileBrowserPreferences.searchEngineKey) != nil {
                Button(CompanionL10n.string(
                    "settings.search_engine.reset", fallback: "Use this device's default"
                )) { model.setBrowserSearchEngine(nil) }
                .accessibilityIdentifier("settings.search.reset")
            }

            Toggle(
                CompanionL10n.string(
                    "settings.sync.browser_settings", fallback: "Sync browser settings"
                ),
                isOn: Binding(
                    get: { model.isBrowserSettingsSyncEnabled },
                    set: { model.setBrowserSettingsSyncEnabled($0) }
                )
            )
            .disabled(!model.isSyncConfigured && !model.isBrowserSettingsSyncEnabled)
            .accessibilityIdentifier("settings.sync.browser-settings")
        } header: {
            Text(CompanionL10n.string("settings.browser.section", fallback: "Browser"))
        } footer: {
            VStack(alignment: .leading, spacing: 4) {
                Text(CompanionL10n.string(
                    "settings.search_engine.footer",
                    fallback: "Search terms are sent only when you choose to navigate."
                ))
                Text(CompanionL10n.string(
                    model.isSyncConfigured ? "settings.sync.browser_settings.detail" :
                        "settings.sync.browser_settings.requires_link",
                    fallback: model.isSyncConfigured ?
                        "Shares supported settings. On iOS, the search engine is applied; desktop-only settings stay as metadata." :
                        "Enable CloudKit sync to share browser settings. Local search works without it."
                ))
            }
        }
    }
}
