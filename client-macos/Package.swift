// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "orc-client",
    platforms: [.macOS(.v14)],
    targets: [
        .executableTarget(
            name: "orc-client",
            path: "Sources/orc-client"
        )
    ]
)
