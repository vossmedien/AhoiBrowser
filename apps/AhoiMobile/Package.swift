// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "AhoiMobile",
    defaultLocalization: "en",
    platforms: [
        .iOS("26.0"),
    ],
    products: [
        .library(
            name: "AhoiMobileCore",
            targets: ["AhoiMobileCore"]
        ),
        .executable(
            name: "AhoiMobileApp",
            targets: ["AhoiMobileApp"]
        ),
    ],
    dependencies: [
        .package(path: "../../spikes/cloudkit"),
    ],
    targets: [
        .target(
            name: "AhoiMobileCore",
            dependencies: [
                .product(name: "AhoiCloudKitSpike", package: "cloudkit")
            ],
            resources: [.process("Resources")]
        ),
        .executableTarget(
            name: "AhoiMobileApp",
            dependencies: ["AhoiMobileCore"]
        ),
        .testTarget(
            name: "AhoiMobileCoreTests",
            dependencies: ["AhoiMobileCore"]
        ),
    ],
    swiftLanguageModes: [.v6]
)
