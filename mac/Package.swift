// swift-tools-version:6.0
import PackageDescription

let package = Package(
    name: "WorkTool",
    platforms: [.macOS(.v14)],
    targets: [
        // 업무 규칙(웹과 같은 JavaScript)·엑셀·내보내기. 화면과 상관없어요.
        .target(name: "WorkToolCore", resources: [.copy("Resources")]),
        // SwiftUI 앱
        .executableTarget(name: "WorkTool", dependencies: ["WorkToolCore"]),
        // 가상 자료로 규칙·엑셀·내보내기를 검증하는 도구
        .executableTarget(name: "worktool-check", dependencies: ["WorkToolCore"]),
    ],
    swiftLanguageModes: [.v5]
)
