import Foundation

public enum SharedTabTargetKind: Int, Codable, Hashable, Sendable {
    case web = 0
    case newTab = 1
    case localOnly = 2
}

public enum SharedTabLocalScheme: String, Codable, Hashable, CaseIterable, Sendable {
    case about, chrome, file, blob, data, javascript, other
    case chromeExtension = "chrome-extension"
}

public enum SharedTabTargetError: Error, Equatable, Sendable {
    case invalidTarget
    case missingPageLink
    case targetMismatch
}

/// Only transport-safe target metadata. Original native paths/code never enter
/// this value; local-only targets retain identity without an executable fallback.
public struct SharedTabTarget: Codable, Equatable, Hashable, Sendable {
    public let kind: SharedTabTargetKind
    public let url: String
    public let localScheme: SharedTabLocalScheme?

    public init(kind: SharedTabTargetKind, url: String, localScheme: SharedTabLocalScheme? = nil) throws {
        self.kind = kind
        self.url = url
        self.localScheme = localScheme
        try validate()
    }

    public func validate() throws {
        switch kind {
        case .web:
            guard localScheme == nil, !url.isEmpty, url.utf8.count <= 131_072,
                  !url.contains("\0"), !url.unicodeScalars.contains(where: { $0.value <= 0x20 || $0.value == 0x7f }),
                  let parsed = URLComponents(string: url),
                  let scheme = parsed.scheme?.lowercased(), ["http", "https"].contains(scheme),
                  let host = parsed.host, !host.isEmpty, parsed.user == nil, parsed.password == nil,
                  parsed.url != nil else { throw SharedTabTargetError.invalidTarget }
        case .newTab:
            guard url.isEmpty, localScheme == nil else { throw SharedTabTargetError.invalidTarget }
        case .localOnly:
            guard url.isEmpty, localScheme != nil else { throw SharedTabTargetError.invalidTarget }
        }
    }

    public func validatePage(isTemporary: Bool) throws {
        try validate()
        guard kind != .newTab || isTemporary else { throw SharedTabTargetError.invalidTarget }
    }

    public func validatePresence(treeNodeID: TreeNodeID?) throws {
        try validate()
        guard kind == .web || treeNodeID != nil else { throw SharedTabTargetError.missingPageLink }
    }

    private enum CodingKeys: String, CodingKey { case kind, url, localScheme }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        try self.init(kind: c.decode(SharedTabTargetKind.self, forKey: .kind),
                      url: c.decode(String.self, forKey: .url),
                      localScheme: c.decodeIfPresent(SharedTabLocalScheme.self, forKey: .localScheme))
    }
}
