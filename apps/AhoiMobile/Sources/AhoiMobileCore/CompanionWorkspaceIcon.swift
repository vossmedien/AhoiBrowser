import Foundation
import SwiftUI
import AhoiCloudKitSpike

struct WorkspaceIcon: View {
    let workspace: Workspace

    var body: some View {
        HStack(spacing: 4) {
            if workspace.icon.isEmpty {
                Image(systemName: "square.stack.3d.up")
            } else {
                Text(workspace.icon)
            }
            if let accent = workspace.accent.flatMap(Color.init(argbHex:)) {
                Circle().fill(accent).frame(width: 7, height: 7)
            }
        }
    }
}

extension Color {
    init?(argbHex: String) {
        let raw = argbHex.trimmingCharacters(
            in: CharacterSet(charactersIn: "#")
        )
        guard let value = UInt32(raw, radix: 16), raw.count == 8 else { return nil }
        self.init(
            .sRGB,
            red: Double((value >> 16) & 0xff) / 255,
            green: Double((value >> 8) & 0xff) / 255,
            blue: Double(value & 0xff) / 255,
            opacity: Double((value >> 24) & 0xff) / 255
        )
    }
}
