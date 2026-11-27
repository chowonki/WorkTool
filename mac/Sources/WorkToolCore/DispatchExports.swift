import AppKit

/// 탁송 결과 캡처(PNG)와 PDF를 웹과 같은 배치로 그려요.
public enum DispatchExports {
    public static let captureMax = 300

    static func number(_ v: Any?) -> String {
        guard let n = v as? NSNumber else { return "—" }
        let f = NumberFormatter(); f.numberStyle = .decimal; f.locale = Locale(identifier: "ko_KR")
        return f.string(from: n) ?? n.stringValue
    }
    static func sum(_ rows: [JSONObject], _ key: String) -> Double {
        rows.reduce(0) { $0 + ((($1[key] as? NSNumber)?.doubleValue) ?? 0) }
    }
    public static func vendorName(_ v: String) -> String { ["A": "한빛탁송", "B": "오픈마일", "C": "렌톡"][v] ?? v }

    static func font(_ size: CGFloat, _ weight: NSFont.Weight = .regular) -> NSFont {
        let name = weight == .bold ? "Pretendard-Bold" : weight == .semibold ? "Pretendard-SemiBold" : "Pretendard-Regular"
        return NSFont(name: name, size: size) ?? .systemFont(ofSize: size, weight: weight)
    }
    static func hex(_ h: UInt32) -> NSColor {
        NSColor(srgbRed: CGFloat((h >> 16) & 255) / 255, green: CGFloat((h >> 8) & 255) / 255, blue: CGFloat(h & 255) / 255, alpha: 1)
    }

    /// 글자가 칸보다 길면 …로 줄여서 그려요.
    static func draw(_ text: String, font: NSFont, color: NSColor, x: CGFloat, midY: CGFloat, width: CGFloat, right: Bool = false) {
        var t = text
        let attrs: (String) -> NSAttributedString = { NSAttributedString(string: $0, attributes: [.font: font, .foregroundColor: color]) }
        if attrs(t).size().width > width {
            while !t.isEmpty && attrs(t + "…").size().width > width { t.removeLast() }
            t += "…"
        }
        let a = attrs(t), size = a.size()
        a.draw(at: NSPoint(x: right ? x + width - size.width : x, y: midY - size.height / 2))
    }

    static func tone(_ r: JSONObject) -> (NSColor, NSColor) {
        let key = (r["_progress"] as? Bool == true) ? "progress" : (r["status"] as? String ?? "")
        let t: [String: (UInt32, UInt32)] = ["confirmed": (0xe6f4ec, 0x1f7a4d), "missing": (0xfdf0e2, 0xa65a10), "progress": (0xfff4dc, 0x8a5a00),
                                             "incomplete": (0xfbe9e9, 0xa33a3a), "cost": (0xfbe9e9, 0xa33a3a), "slack": (0xe6effc, 0x1d4fa8),
                                             "carrier": (0xe6f4ec, 0x1f7a4d), "duplicate": (0xf1f1f4, 0x555b66)]
        let c = t[key] ?? t["duplicate"]!
        return (hex(c.0), hex(c.1))
    }

    /// 웹 captureCanvas와 같은 표 이미지(2배 해상도 PNG)
    public static func capturePNG(_ rows: [JSONObject], scopeLabel: String, date: Date = Date()) -> Data {
        let S: CGFloat = 2, PAD: CGFloat = 28, ROW: CGFloat = 36, HEAD: CGFloat = 40, TOP: CGFloat = 92
        let cols: [(String, CGFloat, Bool)] = [("탁송일정", 150, false), ("차량번호", 118, false), ("주유비용/충전비용(원)", 150, true),
                                               ("탁송비용(원)", 112, true), ("부가세(원)", 96, true), ("조회 상태", 150, false), ("업체", 96, false)]
        let W = PAD * 2 + cols.reduce(0) { $0 + $1.1 }, H = TOP + HEAD + CGFloat(rows.count) * ROW + 70
        let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(W * S), pixelsHigh: Int(H * S), bitsPerSample: 8, samplesPerPixel: 4,
                                   hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        rep.size = NSSize(width: W, height: H)
        NSGraphicsContext.saveGraphicsState()
        let ctx = NSGraphicsContext(bitmapImageRep: rep)!
        NSGraphicsContext.current = ctx
        // 위에서 아래로 그리기 위해 좌표를 뒤집어요.
        let flip = NSAffineTransform(); flip.translateX(by: 0, yBy: H); flip.scaleX(by: 1, yBy: -1); flip.concat()
        let flippedCtx = NSGraphicsContext(cgContext: ctx.cgContext, flipped: true)
        NSGraphicsContext.current = flippedCtx

        NSColor.white.setFill(); NSRect(x: 0, y: 0, width: W, height: H).fill()
        let df = DateFormatter(); df.dateFormat = "yyyy-MM-dd HH:mm"
        draw("탁송 조회 결과", font: font(22, .bold), color: hex(0x22324a), x: PAD, midY: 37, width: W)
        draw("\(scopeLabel) · 차량 \(rows.count)대 · \(df.string(from: date))", font: font(13), color: hex(0x5b6d85), x: PAD, midY: 66, width: W)
        var y = TOP
        hex(0xeef3fb).setFill(); NSRect(x: PAD, y: y, width: W - PAD * 2, height: HEAD).fill()
        var x = PAD
        for (h, w, r) in cols { draw(h, font: font(12.5, .bold), color: hex(0x3d5170), x: x + 12, midY: y + HEAD / 2, width: w - 24, right: r); x += w }
        y += HEAD
        for (i, r) in rows.enumerated() {
            if i % 2 == 1 { hex(0xf7f9fc).setFill(); NSRect(x: PAD, y: y, width: W - PAD * 2, height: ROW).fill() }
            let sched = (r["scheduleConflict"] as? Bool == true) && (r["schedule"] as? String ?? "").isEmpty ? "선택 필요" : ((r["schedule"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? "—")
            let fee = (r["feeConflict"] as? Bool == true) && r["dispatchFee"] is NSNull ? "선택 필요" : number(r["dispatchFee"])
            let amount = (r["slackPending"] as? Bool == true) ? "확인 중" : number(r["amount"])
            let vals = [sched, r["plate"] as? String ?? "", amount, fee, number(r["vat"]), r["_label"] as? String ?? "", (r["vendor"] as? String).map { $0.isEmpty ? "—" : $0 } ?? "—"]
            x = PAD
            for (c, v) in vals.enumerated() {
                let (_, w, right) = cols[c]
                if c == 5 {
                    let (bg, fg) = tone(r), f = font(12, .bold)
                    let tw = min(NSAttributedString(string: v, attributes: [.font: f]).size().width, w - 30) + 16
                    bg.setFill(); NSBezierPath(roundedRect: NSRect(x: x + 10, y: y + 8, width: tw, height: ROW - 16), xRadius: 9, yRadius: 9).fill()
                    draw(v, font: f, color: fg, x: x + 18, midY: y + ROW / 2, width: w - 30)
                } else {
                    draw(v, font: c == 1 ? font(14, .bold) : font(13.5), color: v == "선택 필요" ? hex(0xa65a10) : hex(0x2a3a52), x: x + 12, midY: y + ROW / 2, width: w - 24, right: right)
                }
                x += w
            }
            hex(0xe3e9f2).setStroke()
            let line = NSBezierPath(); line.move(to: NSPoint(x: PAD, y: y + ROW + 0.5)); line.line(to: NSPoint(x: W - PAD, y: y + ROW + 0.5)); line.lineWidth = 1; line.stroke()
            y += ROW
        }
        let f = NumberFormatter(); f.numberStyle = .decimal
        let total = "합계 · 주유비용/충전비용 \(f.string(from: sum(rows, "amount") as NSNumber)!)원 · 탁송비용 \(f.string(from: sum(rows, "dispatchFee") as NSNumber)!)원 · 부가세 \(f.string(from: sum(rows, "vat") as NSNumber)!)원"
        draw(total, font: font(13, .bold), color: hex(0x22324a), x: PAD, midY: y + 26, width: W - PAD * 2)
        hex(0xd6dfeb).setStroke(); NSBezierPath(rect: NSRect(x: PAD + 0.5, y: TOP + 0.5, width: W - PAD * 2 - 1, height: HEAD + CGFloat(rows.count) * ROW - 1)).stroke()
        NSGraphicsContext.restoreGraphicsState()
        return rep.representation(using: .png, properties: [:]) ?? Data()
    }

    /// 웹 pdfBlob과 같은 A4 세로 표(쪽당 19대)
    public static func pdf(_ rows: [JSONObject], date: Date = Date()) -> Data {
        let mm: CGFloat = 72 / 25.4
        let pageRect = CGRect(x: 0, y: 0, width: 210 * mm, height: 297 * mm)
        let data = NSMutableData()
        var box = pageRect
        guard let consumer = CGDataConsumer(data: data as CFMutableData),
              let pdf = CGContext(consumer: consumer, mediaBox: &box, [kCGPDFContextTitle: "탁송체크 · 조회결과", kCGPDFContextCreator: "WorkTool for Mac"] as CFDictionary) else { return Data() }
        let pages = max(1, Int(ceil(Double(rows.count) / 19)))
        let xs: [CGFloat] = [14, 47, 76, 102, 130, 154], widths: [CGFloat] = [33, 29, 26, 28, 24, 42]
        let df = DateFormatter(); df.locale = Locale(identifier: "ko_KR"); df.dateStyle = .medium; df.timeStyle = .medium
        for p in 0..<pages {
            pdf.beginPDFPage(nil)
            NSGraphicsContext.saveGraphicsState()
            pdf.translateBy(x: 0, y: pageRect.height); pdf.scaleBy(x: mm, y: -mm)   // mm 단위, 위에서 아래로
            NSGraphicsContext.current = NSGraphicsContext(cgContext: pdf, flipped: true)
            let pt: (CGFloat) -> CGFloat = { $0 * 0.3528 }  // 글자 pt → mm
            draw("탁송체크 · 조회결과", font: font(pt(18)), color: hex(0x244C3B), x: 14, midY: 18.5, width: 180)
            draw("차량 \(rows.count)대 · \(df.string(from: date))", font: font(pt(8)), color: NSColor(white: 130 / 255, alpha: 1), x: 14, midY: 29, width: 180)
            draw("완료 기준 / 10원 단위 반올림 / C업체 금액 제외 / 차량번호 중복 제거", font: font(pt(8)), color: NSColor(white: 130 / 255, alpha: 1), x: 14, midY: 35, width: 180)
            hex(0x244C3B).setFill(); NSRect(x: 14, y: 43, width: 182, height: 11).fill()
            for (i, h) in ["탁송일정", "차량번호", "주유·충전(원)", "탁송비(원)", "부가세(원)", "조회 상태"].enumerated() {
                draw(h, font: font(pt(8)), color: .white, x: xs[i] + 2, midY: 48.5, width: widths[i])
            }
            for (i, r) in rows.dropFirst(p * 19).prefix(19).enumerated() {
                let y = 54 + CGFloat(i) * 10.7
                if i % 2 == 0 { hex(0xF5F8F5).setFill(); NSRect(x: 14, y: y, width: 182, height: 10.7).fill() }
                let sched = (r["schedule"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? "—"
                let vals = [sched, r["plate"] as? String ?? "", (r["slackPending"] as? Bool == true) ? "확인 중" : number(r["amount"]),
                            number(r["dispatchFee"]), number(r["vat"]), r["_label"] as? String ?? ""]
                for (c, v) in vals.enumerated() {
                    draw(v, font: font(pt(7)), color: hex(0x37453E), x: xs[c] + 2, midY: y + 5.35, width: widths[c] - 5)
                }
            }
            draw("\(p + 1) / \(pages)", font: font(pt(8)), color: NSColor(white: 150 / 255, alpha: 1), x: 150, midY: 285, width: 46, right: true)
            NSGraphicsContext.restoreGraphicsState()
            pdf.endPDFPage()
        }
        pdf.closePDF()
        return data as Data
    }
}
