// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "AhoiCompanion",
    platforms: [
        .iOS("26.0"),
        .macOS("26.0"),
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
            ],
            resources: [.process("Resources")]
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
