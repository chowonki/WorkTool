import SwiftUI
import AppKit
import UniformTypeIdentifiers

/// macOS 27 Golden Gate 기준: 시스템 글꼴, 밝기에 맞춰 바뀌는 상태 색, WorkTool 녹색 강조색
enum Theme {
    static let accent = dyn(0x267358, 0x4fb38c)
    static let ok = dyn(0x1f7a4d, 0x4cc38a)
    static let warn = dyn(0xa65a10, 0xf0a04b)
    static let bad = dyn(0xb42f2f, 0xff6b6b)
    static let info = dyn(0x1d4fa8, 0x6ea1ff)
    static let progress = dyn(0x8a5a00, 0xe0b44c)
    static let teal = dyn(0x0f8b8d, 0x3fc7c9)
    static let purple = dyn(0x7a4cc2, 0xb28cff)
    static let pink = dyn(0xc23f7a, 0xff7fb4)
    static let indigo = dyn(0x4b56d2, 0x8f97ff)
    static let brown = dyn(0x8a6233, 0xc9a06a)

    /// 라이트·다크 색을 따로 정해요.
    static func dyn(_ light: UInt32, _ dark: UInt32) -> Color {
        func c(_ v: UInt32) -> NSColor {
            NSColor(srgbRed: CGFloat((v >> 16) & 0xff) / 255, green: CGFloat((v >> 8) & 0xff) / 255, blue: CGFloat(v & 0xff) / 255, alpha: 1)
        }
        return Color(nsColor: NSColor(name: nil) { a in a.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua ? c(dark) : c(light) })
    }

    static func font(_ size: CGFloat, _ weight: Font.Weight = .regular) -> Font { .system(size: size, weight: weight) }
    static let body = font(13)
    static let small = font(11.5)
    static let mono = Font.system(size: 12.5, design: .monospaced)

    static func statusColor(_ status: String, progress: Bool = false) -> Color {
        if progress { return Theme.progress }
        switch status {
        case "confirmed", "carrier": return ok
        case "missing": return warn
        case "slack": return info
        case "incomplete", "cost": return bad
        default: return .secondary
        }
    }
}

/// 상태 표시: 색 알약(점 + 색 글자)
struct StatusDot: View {
    let text: String
    let color: Color
    var body: some View {
        HStack(spacing: 5) {
            Circle().fill(color).frame(width: 6, height: 6)
            Text(text).font(Theme.font(12, .semibold)).foregroundStyle(color).lineLimit(1)
        }
        .padding(.horizontal, 8).padding(.vertical, 2)
        .background(Capsule().fill(color.opacity(0.14)))
    }
}

/// 시스템 설정처럼 색 사각형 위 흰 아이콘
struct IconTile: View {
    let symbol: String
    let color: Color
    var size: CGFloat = 20
    var body: some View {
        Image(systemName: symbol)
            .font(.system(size: size * 0.55, weight: .semibold))
            .foregroundStyle(.white)
            .frame(width: size, height: size)
            .background(RoundedRectangle(cornerRadius: size * 0.26, style: .continuous)
                .fill(LinearGradient(colors: [color.opacity(0.85), color], startPoint: .top, endPoint: .bottom)))
    }
}

/// 작은 글자 배지 (입력칸의 '추출함' 등)
struct Badge: View {
    let text: String
    let color: Color
    var body: some View {
        Text(text).font(Theme.font(11, .semibold)).foregroundStyle(color)
            .padding(.horizontal, 6).padding(.vertical, 1)
            .background(Capsule().fill(color.opacity(0.13)))
            .lineLimit(1)
    }
}

/// 도구 막대의 주 동작 버튼 (도구 막대는 기본 강조 스타일을 지워서 직접 그려요)
struct ProminentToolbarStyle: ButtonStyle {
    @Environment(\.isEnabled) private var enabled
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(Theme.font(13, .semibold))
            .foregroundStyle(.white)
            .padding(.horizontal, 12).frame(height: 28)
            .background(Capsule().fill(Theme.accent.opacity(enabled ? (configuration.isPressed ? 0.8 : 1) : 0.45)))
            .contentShape(Capsule())
    }
}

/// 인스펙터·본문 구역 제목 (카드 없이 글자와 구분선만)
struct SectionHeader: View {
    let title: String
    var icon: String? = nil
    var color: Color = Theme.accent
    var trailing: AnyView? = nil
    init(_ title: String, icon: String? = nil, color: Color = Theme.accent, trailing: AnyView? = nil) {
        self.title = title; self.icon = icon; self.color = color; self.trailing = trailing
    }
    var body: some View {
        HStack(spacing: 7) {
            if let icon { IconTile(symbol: icon, color: color, size: 18) }
            Text(title).font(Theme.font(12, .semibold)).foregroundStyle(color)
            Spacer()
            trailing
        }
    }
}

/// 오른쪽 인스펙터: 위에 범위 줄과 같은 높이의 제목 줄, 아래는 스크롤 내용
struct InspectorScroll<Content: View>: View {
    var title: String
    var icon: String = "square.and.pencil"
    var color: Color = Theme.accent
    @ViewBuilder var content: Content
    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                IconTile(symbol: icon, color: color, size: 20)
                Text(title).font(Theme.font(13, .semibold))
                Spacer()
            }
            .padding(.horizontal, 16).frame(height: 34)
            Divider()
            ScrollView { content }
        }
    }
}

/// 표 아래 상태 줄
struct FooterBar<Content: View>: View {
    @ViewBuilder var content: Content
    var body: some View {
        VStack(spacing: 0) {
            Divider()
            HStack(spacing: 12) { content }
                .font(Theme.small).foregroundStyle(.secondary)
                .padding(.horizontal, 14).frame(height: 26)
        }
    }
}

/// 표 위 범위·필터 줄
struct ScopeBar<Content: View>: View {
    @ViewBuilder var content: Content
    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 10) { content }
                .controlSize(.small)
                .padding(.horizontal, 12).frame(height: 34)
            Divider()
        }
    }
}

/// 아래쪽에 잠깐 뜨는 알림
@MainActor final class Toaster: ObservableObject {
    @Published var message: String?
    private var task: Task<Void, Never>?
    func show(_ text: String) {
        message = text
        task?.cancel()
        task = Task { try? await Task.sleep(for: .seconds(2.8)); if !Task.isCancelled { message = nil } }
    }
}

struct ToastOverlay: View {
    @ObservedObject var toaster: Toaster
    var body: some View {
        VStack {
            Spacer()
            if let m = toaster.message {
                Text(m).font(Theme.font(13, .medium))
                    .padding(.horizontal, 16).padding(.vertical, 9)
                    .multilineTextAlignment(.center).frame(maxWidth: 520)
                    .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
                    .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous).strokeBorder(Color.primary.opacity(0.1)))
                    .shadow(color: .black.opacity(0.12), radius: 14, y: 6)
                    .padding(.bottom, 24)
                    .transition(.move(edge: .bottom).combined(with: .opacity))
            }
        }
        .animation(.easeOut(duration: 0.2), value: toaster.message)
        .allowsHitTesting(false)
    }
}

/// 대화상자·저장·클립보드
@MainActor enum UI {
    static func alert(_ title: String, _ text: String, details: String = "", style: NSAlert.Style = .informational) {
        let a = NSAlert(); a.messageText = title; a.informativeText = text; a.alertStyle = style
        if !details.isEmpty { a.accessoryView = detailsView(details) }
        a.runModal()
    }

    static func confirm(_ title: String, _ text: String, ok: String = "확인", cancel: String = "취소", destructive: Bool = false, details: String = "") -> Bool {
        let a = NSAlert(); a.messageText = title; a.informativeText = text; a.alertStyle = .warning
        let b = a.addButton(withTitle: ok); a.addButton(withTitle: cancel)
        if destructive { b.hasDestructiveAction = true }
        if !details.isEmpty { a.accessoryView = detailsView(details) }
        return a.runModal() == .alertFirstButtonReturn
    }

    /// 암호 입력. 취소하면 nil
    static func password(_ title: String, _ text: String) -> String? {
        let a = NSAlert(); a.messageText = title; a.informativeText = text
        let field = NSSecureTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
        a.accessoryView = field
        a.addButton(withTitle: "열기"); a.addButton(withTitle: "취소")
        a.window.initialFirstResponder = field
        return a.runModal() == .alertFirstButtonReturn ? field.stringValue : nil
    }

    private static func detailsView(_ text: String) -> NSView {
        let scroll = NSScrollView(frame: NSRect(x: 0, y: 0, width: 420, height: 160))
        scroll.hasVerticalScroller = true; scroll.borderType = .bezelBorder
        let tv = NSTextView(frame: scroll.bounds)
        tv.string = text; tv.isEditable = false; tv.font = .systemFont(ofSize: 12)
        tv.autoresizingMask = [.width]
        scroll.documentView = tv
        return scroll
    }

    /// 저장 위치를 묻고 저장해요. 취소하면 false
    static func save(_ data: Data, name: String, types: [UTType]) -> URL? {
        let p = NSSavePanel()
        p.nameFieldStringValue = name
        p.allowedContentTypes = types
        p.canCreateDirectories = true
        p.directoryURL = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
        guard p.runModal() == .OK, let url = p.url else { return nil }
        do { try data.write(to: url, options: .atomic); return url }
        catch { alert("파일을 저장하지 못했어요", error.localizedDescription, style: .warning); return nil }
    }

    static func copy(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    /// 복사한 값을 그대로 보여줄 글. 길면 앞 5줄 + "외 n줄"
    static func copyPreview(_ text: String) -> String {
        let lines = text.replacingOccurrences(of: "\r\n", with: "\n").split(separator: "\n")
            .map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        guard !lines.isEmpty else { return "복사했어요." }
        let shown = lines.prefix(5).map { $0.count > 80 ? String($0.prefix(80)) + "…" : $0 }.joined(separator: "\n")
        return lines.count > 5 ? "\(shown)\n외 \(lines.count - 5)줄 (총 \(lines.count)줄 복사)" : shown
    }

    static func copyImage(_ png: Data) {
        NSPasteboard.general.clearContents()
        if let img = NSImage(data: png) { NSPasteboard.general.writeObjects([img]) }
        NSPasteboard.general.setData(png, forType: .png)
    }

    nonisolated static func stamp(_ format: String, _ date: Date = Date()) -> String {
        let f = DateFormatter(); f.locale = Locale(identifier: "en_US_POSIX"); f.dateFormat = format
        return f.string(from: date)
    }

    nonisolated static func won(_ n: Double) -> String {
        let f = NumberFormatter(); f.numberStyle = .decimal; f.locale = Locale(identifier: "ko_KR")
        return f.string(from: NSNumber(value: n)) ?? String(Int(n))
    }
}
