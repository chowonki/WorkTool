import Foundation
import CoreText

/// 앱 리소스(규칙 JS·글꼴·변경 이력)를 찾아요. .app 안(Contents/Resources)과 `swift run` 둘 다 지원해요.
public enum AppResources {
    static let bundleName = "WorkTool_WorkToolCore.bundle"

    public static let root: URL = {
        var candidates: [URL] = []
        if let r = Bundle.main.resourceURL { candidates.append(r.appendingPathComponent(bundleName)) }
        candidates.append(Bundle.main.bundleURL.appendingPathComponent(bundleName))
        if let exe = Bundle.main.executableURL?.deletingLastPathComponent() { candidates.append(exe.appendingPathComponent(bundleName)) }
        for c in candidates {
            let r = c.appendingPathComponent("Resources")
            if FileManager.default.fileExists(atPath: r.appendingPathComponent("js/dispatch.js").path) { return r }
        }
        return Bundle.module.resourceURL!.appendingPathComponent("Resources")
    }()

    public static func url(_ path: String) -> URL { root.appendingPathComponent(path) }
    public static func text(_ path: String) -> String { (try? String(contentsOf: url(path), encoding: .utf8)) ?? "" }
    public static var rulesVersion: String { text("rules-version.txt").trimmingCharacters(in: .whitespacesAndNewlines) }

    /// Pretendard 글꼴을 이 앱 안에서만 쓰도록 등록해요.
    public static func registerFonts() {
        let dir = url("Fonts")
        let files = (try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)) ?? []
        for f in files where f.pathExtension.lowercased() == "otf" {
            CTFontManagerRegisterFontsForURL(f as CFURL, .process, nil)
        }
    }
}
