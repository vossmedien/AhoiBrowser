// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "AhoiCompanion",
    platforms: [
        .iOS(.v17),
        .macOS(.v14),
    ],
    products: [
        .library(
            name: "AhoiCompanionCore",
            targets: ["AhoiCompanionCore"]
        ),
        .executable(
            name: "AhoiCompanionApp",
            targets: ["AhoiCompanionApp"]
        ),
    ],
    dependencies: [
        .package(path: "../../spikes/cloudkit"),
    ],
    targets: [
        .target(
            name: "AhoiCompanionCore",
            dependencies: [
                .product(name: "AhoiCloudKitSpike", package: "cloudkit")
            ]
        ),
        .executableTarget(
            name: "AhoiCompanionApp",
            dependencies: ["AhoiCompanionCore"]
        ),
        .testTarget(
            name: "AhoiCompanionCoreTests",
            dependencies: ["AhoiCompanionCore"]
        ),
    ],
    swiftLanguageModes: [.v6]
)
