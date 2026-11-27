import Foundation
import WorkToolCore

// 가상 자료로 규칙·엑셀·내보내기를 검증해요.
// 사용법: worktool-check <WorkTool 저장소 폴더> [결과 폴더]
let args = CommandLine.arguments
guard args.count >= 2 else { print("사용법: worktool-check <WorkTool 저장소 폴더> [결과 폴더]"); exit(2) }
let web = URL(fileURLWithPath: args[1])
let out = URL(fileURLWithPath: args.count > 2 ? args[2] : FileManager.default.currentDirectoryPath + "/check-output")
try? FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
AppResources.registerFonts()

var failures = 0
func expect(_ ok: Bool, _ what: String) { print(ok ? "  ✓ \(what)" : "  ✗ \(what)"); if !ok { failures += 1 } }

let rules = Rules.shared
let sema = DispatchSemaphore(value: 0)
Task {
    do {
        print("탁송체크")
        var files: [JSONObject] = []
        for name in ["한빛탁송_가상.xlsx", "오픈마일_가상.xlsx", "렌톡_가상.xlsx"] {
            let d = try Data(contentsOf: web.appendingPathComponent("scripts/fixtures/\(name)"))
            let r = try await rules.readVendorFile(d, name: name, password: nil)
            expect(((r["records"] as? [Any])?.count ?? 0) > 0, "\(name): 기록 \((r["records"] as? [Any])?.count ?? 0)건")
            files.append(r)
        }
        let enc = web.appendingPathComponent("windows/WorkTool.Tests/Fixtures/encrypted.xlsx")
        if let d = try? Data(contentsOf: enc) {
            expect(Rules.isEncryptedExcel(d), "암호 파일 판별")
            let r = try await rules.readVendorFile(d, name: "encrypted.xlsx", password: "fixture-password")
            expect(((r["records"] as? [JSONObject])?.first?["plate"] as? String) == "123가4567", "암호 파일 읽기")
            do { _ = try await rules.readVendorFile(d, name: "encrypted.xlsx", password: "wrong"); expect(false, "틀린 암호 거부") }
            catch { expect(error.localizedDescription.contains("암호"), "틀린 암호 거부: \(error.localizedDescription)") }
        }
        let list = try String(contentsOf: web.appendingPathComponent("scripts/fixtures/차량목록.txt"), encoding: .utf8)
        let parsed = try rules.parseInput(list)
        expect(parsed.vehicles.count == 10 && parsed.invalid.isEmpty, "차량목록 \(parsed.vehicles.count)대 인식")
        var rows = try rules.match(vehicles: parsed.vehicles, files: files)
        expect(rows.count == 10, "조회 결과 \(rows.count)대")
        for r in rows { print("    \(r["plate"] ?? "") · \(r["_label"] ?? "") · 일정 \(r["schedule"] ?? "") · 금액 \(r["amount"] ?? "-") · 비용 \(r["dispatchFee"] ?? "-") · \(r["_notes"] ?? "")") }
        if let i = rows.firstIndex(where: { $0["scheduleConflict"] as? Bool == true }), let c = (rows[i]["scheduleCandidates"] as? [JSONObject])?.first {
            rows[i] = try rules.useCandidate(rows[i], field: "schedule", id: c["id"] as? String ?? "")
            expect(rows[i]["scheduleConflict"] as? Bool == false, "중첩 일정 선택")
        }
        let slackTarget = rows.first { $0["cInquiry"] as? Bool == true }
        if let p = slackTarget?["plate"] as? String {
            let r = try rules.applySlackReplies("\(p) - 12,345원\n잘못된줄", rows: rows)
            expect(r.applied == 1 && r.issues.count == 1, "슬랙봇 회신 반영 \(r.applied)대 · 확인 \(r.issues.count)건")
            rows = r.rows
        }
        let txt = try rules.textExport(rows)
        try txt.write(to: out.appendingPathComponent("탁송조회.txt"), atomically: true, encoding: .utf8)
        expect(txt.hasPrefix("\u{feff}탁송일정\t차량번호"), "TXT 내보내기")
        let xlsx = try await rules.excelExport(rules.unique(rows))
        try xlsx.write(to: out.appendingPathComponent("탁송조회.xlsx"))
        expect(xlsx.prefix(2) == Data([0x50, 0x4B]), "엑셀 내보내기 \(xlsx.count)바이트")
        let png = DispatchExports.capturePNG(rules.unique(rows), scopeLabel: "전체 조회 결과")
        try png.write(to: out.appendingPathComponent("탁송캡처.png"))
        expect(png.count > 1000, "캡처 PNG \(png.count)바이트")
        let pdf = DispatchExports.pdf(rules.unique(rows))
        try pdf.write(to: out.appendingPathComponent("탁송조회.pdf"))
        expect(pdf.prefix(4) == Data("%PDF".utf8), "PDF \(pdf.count)바이트")
    } catch { print("  ✗ 탁송 오류: \(error.localizedDescription)"); failures += 1 }

    do {
        print("이체파일")
        let thread = "현대 쏘나타 123가4567\nhttps://example.com/vehicle/987654321\n국민은행\n123-456-789012\n35,000원\n\n기아 K5 45나6789\n신한은행\n110-123-456789\n1억 2천만원"
        let res = try rules.transferCall("Transfer.extractThread(__raw,0,'t1','')", rows: [], extra: ["__raw": thread]) as? JSONObject ?? [:]
        let added = res["added"] as? [JSONObject] ?? []
        expect(added.count == 2, "추출 \(added.count)건")
        let split = try rules.transferCall("Transfer.splitAll(__rows)", rows: added) as? [JSONObject] ?? []
        expect(split.count == 3, "1억 초과 분할 → \(split.count)줄")
        let checks = try rules.transferCall("Transfer.check(__rows)", rows: split) as? [JSONObject] ?? []
        expect(checks.map { $0["state"] as? String ?? "" } == ["ok", "warn", "warn"], "검사 결과 \(checks.map { $0["state"] as? String ?? "" })")
        let comments = try rules.transferCall("Transfer.comments(__rows)", rows: split) as? [String] ?? []
        print("    코멘트: \(comments)")
        let name = try rules.transferCall("Transfer.fileName(__rows,'20261127_0930')", rows: split) as? String ?? ""
        expect(name.hasPrefix("하나은행_다계좌이체_20261127_0930_123가4567"), "파일명 \(name)")
        let x = try rules.transferXlsx(split)
        try x.write(to: out.appendingPathComponent(name))
        expect(x.prefix(2) == Data([0x50, 0x4B]), "이체 엑셀 \(x.count)바이트")
        let banks = rules.bankGroups.flatMap(\.banks)
        expect(banks.contains("국민") && rules.bankCodes["국민"] == "004", "은행 목록 \(banks.count)개")
    } catch { print("  ✗ 이체 오류: \(error.localizedDescription)"); failures += 1 }
    sema.signal()
}
sema.wait()
print(failures == 0 ? "모두 통과 · 결과: \(out.path)" : "실패 \(failures)건")
exit(failures == 0 ? 0 : 1)
