// swift-tools-version: 5.10

import PackageDescription

let package = Package(
    name: "AhoiCloudKitSpike",
    platforms: [
        .macOS(.v13),
        .iOS(.v16),
    ],
    products: [
        .library(
            name: "AhoiCloudKitSpike",
            targets: ["AhoiCloudKitSpike"]
        ),
    ],
    targets: [
        .target(name: "AhoiCloudKitSpike"),
        .testTarget(
            name: "AhoiCloudKitSpikeTests",
            dependencies: ["AhoiCloudKitSpike"]
        ),
    ],
    swiftLanguageVersions: [.v5]
)
