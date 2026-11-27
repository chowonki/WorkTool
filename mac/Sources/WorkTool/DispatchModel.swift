import SwiftUI
import UniformTypeIdentifiers
import WorkToolCore

/// 조회 결과 한 줄 (웹 DispatchCore.match 결과 그대로 + 표시용 값)
struct DRow: Identifiable {
    let id: Int
    var raw: JSONObject

    func str(_ k: String) -> String { raw[k] as? String ?? "" }
    func num(_ k: String) -> Double? { (raw[k] as? NSNumber)?.doubleValue }
    func flag(_ k: String) -> Bool { raw[k] as? Bool ?? false }

    var plate: String { str("plate") }
    var schedule: String { str("schedule") }
    var status: String { str("status") }
    var label: String { str("_label") }
    var notes: String { str("_notes") }
    var vendor: String { str("vendor") }
    var origin: String { str("origin") }
    var carrierType: String { str("carrierType") }
    var inputDate: String { str("date") }
    var sources: String { str("sources") }
    var amount: Double? { num("amount") }
    var fee: Double? { num("dispatchFee") }
    var vat: Double? { num("vat") }
    var progress: Bool { flag("_progress") }
    var unresolved: Bool { flag("_unresolved") }
    var filters: Set<String> { Set(raw["_filters"] as? [String] ?? []) }
    var rawStates: [String] { raw["rawStates"] as? [String] ?? [] }

    struct Candidate: Identifiable { let id: String; let value: String; let vendor: String; let source: String; let state: String }
    func candidates(_ field: String) -> [Candidate] {
        (raw[field == "schedule" ? "scheduleCandidates" : "feeCandidates"] as? [JSONObject] ?? []).map {
            let v = $0["value"]
            let text = (v as? NSNumber).map { UI.won($0.doubleValue) } ?? (v as? String ?? "")
            return Candidate(id: $0["id"] as? String ?? "", value: text, vendor: DispatchExports.vendorName($0["vendor"] as? String ?? ""),
                             source: $0["source"] as? String ?? "", state: $0["state"] as? String ?? "")
        }
    }
    func distinctValues(_ field: String) -> Int { Set(candidates(field).map(\.value)).count }

    // 정렬용 값
    var sortSchedule: String { schedule.isEmpty ? "~" : schedule }
    var sortAmount: Double { amount ?? -1 }
    var sortFee: Double { fee ?? -1 }
}

/// 업체 파일 한 개
struct VendorFile: Identifiable {
    let id = UUID()
    let key: String
    let name: String
    let encrypted: Bool
    let result: JSONObject
    var records: Int { (result["records"] as? [Any])?.count ?? 0 }
    var vendors: String {
        let v = Set((result["records"] as? [JSONObject] ?? []).compactMap { $0["vendor"] as? String })
        return v.sorted().map(DispatchExports.vendorName).joined(separator: "·")
    }
    var ignored: Int { (result["ignored"] as? NSNumber)?.intValue ?? 0 }
}

@MainActor
final class DispatchModel: ObservableObject {
    static let filterTitles: [(String, String)] = [("all", "전체"), ("review", "확인 필요"), ("confirmed", "금액 확인"), ("progress", "진행 중"),
                                                  ("missing", "미조회"), ("jeju", "제주 출발"), ("carrier", "캐리어탁송"), ("slack", "슬랙봇 문의"), ("slack-replies", "슬랙 회신 반영")]
    static let scopes: [(String, String)] = [("all", "전체 조회 결과"), ("confirmed", "금액 확인 차량"), ("visible", "현재 표시 목록")]

    let rules = Rules.shared
    let toaster = Toaster()

    @Published var input = "" {
        didSet {
            guard !cleaningInput else { return }
            // 붙여넣기처럼 한 번에 여러 글자가 들어오면 빈 줄을 알아서 지워요.
            // 한 글자씩 칠 때나 지울 때는 그대로 둬서 줄 추가·편집을 막지 않아요.
            if input.count - oldValue.count > 1 {
                let cleaned = Self.dropBlankLines(input)
                if cleaned != input { cleaningInput = true; input = cleaned; cleaningInput = false }
            }
            parseInput()
        }
    }
    private var cleaningInput = false

    /// 빈 줄(공백만 있는 줄 포함)을 지워요. 마지막 줄바꿈 하나는 남겨서 이어 입력할 수 있게 해요.
    static func dropBlankLines(_ text: String) -> String {
        let lines = text.replacingOccurrences(of: "\r\n", with: "\n").replacingOccurrences(of: "\r", with: "\n")
            .components(separatedBy: "\n").filter { !$0.trimmingCharacters(in: .whitespaces).isEmpty }
        let joined = lines.joined(separator: "\n")
        return text.hasSuffix("\n") && !joined.isEmpty ? joined + "\n" : joined
    }
    @Published private(set) var parsed: Rules.ParsedInput?
    @Published var files: [VendorFile] = [] { didSet { stale = !results.isEmpty } }
    @Published var results: [DRow] = []
    @Published var stale = false
    @Published var filter = "all"
    @Published var search = ""
    @Published var vendorFilter = "all"
    @Published var stateFilter = "all"
    @Published var selection = Set<Int>()
    @Published var completed = Set<String>() { didSet { saveMarks() } }
    @Published var copied = Set<String>() { didSet { saveMarks() } }
    @Published var scope = "all"
    @Published var busy = false
    @Published var status = "차량목록과 업체 파일을 넣고 조회해 주세요."
    @Published var sortOrder: [KeyPathComparator<DRow>] = []
    @Published var showSlack = false
    @Published var slackText = ""
    @Published var slackReport = ""

    init() {
        // '복사함·완료' 표시는 오늘 하루 동안 이 Mac에 남겨요 (웹과 같아요).
        if let d = UserDefaults.standard.dictionary(forKey: "dispatch-marks"), d["date"] as? String == UI.stamp("yyyy-MM-dd") {
            completed = Set(d["done"] as? [String] ?? []); copied = Set(d["copied"] as? [String] ?? [])
        }
    }

    private func saveMarks() {
        UserDefaults.standard.set(["date": UI.stamp("yyyy-MM-dd"), "done": Array(completed), "copied": Array(copied)], forKey: "dispatch-marks")
    }

    private func parseInput() {
        parsed = input.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? nil : try? rules.parseInput(input)
        if !results.isEmpty { stale = true }
    }

    // MARK: 파일 첨부

    func pickFiles() {
        let p = NSOpenPanel()
        p.allowsMultipleSelection = true
        p.canChooseDirectories = false
        p.allowedContentTypes = [UTType(filenameExtension: "xlsx")!]
        p.message = "한빛탁송·오픈마일·렌톡에서 받은 .xlsx 시트를 골라 주세요."
        if p.runModal() == .OK { Task { await addFiles(p.urls) } }
    }

    func addFiles(_ urls: [URL]) async {
        guard !busy, !urls.isEmpty else { return }
        busy = true
        var added = 0
        var errors: [String] = [], layoutWarnings: [String] = []
        for url in urls {
            let name = url.lastPathComponent
            guard url.pathExtension.lowercased() == "xlsx" else { errors.append("\(name): .xlsx 형식만 지원해요."); continue }
            let attrs = try? FileManager.default.attributesOfItem(atPath: url.path)
            let size = (attrs?[.size] as? NSNumber)?.intValue ?? 0
            guard size <= 40 * 1024 * 1024 else { errors.append("\(name): 파일은 40MB까지 지원해요."); continue }
            let modified = (attrs?[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0
            let key = "\(name)/\(size)/\(modified)"
            if files.contains(where: { $0.key == key }) { errors.append("\(name): 이미 첨부한 파일이에요."); continue }
            do {
                status = "\(name) 읽는 중…"
                let data = try Data(contentsOf: url)
                let encrypted = Rules.isEncryptedExcel(data)
                var password: String?
                if encrypted {
                    password = UI.password("엑셀 열기 암호", "\(name)\n암호는 이 파일을 읽을 때만 사용하며 저장하지 않아요.")
                    if password == nil { continue }
                }
                let result = try await rules.readVendorFile(data, name: name, password: password)
                password = nil
                files.append(VendorFile(key: key, name: name, encrypted: encrypted, result: result))
                added += 1
                for w in result["warnings"] as? [String] ?? [] { layoutWarnings.append("\(name)\n\(w)") }
            } catch {
                errors.append("\(name)\n\(error.localizedDescription)")
            }
        }
        busy = false
        let total = files.reduce(0) { $0 + $1.records }
        status = "\(added)개 파일 추가 · 총 \(files.count)개 파일 / \(UI.won(Double(total)))개 기록"
        if !errors.isEmpty { UI.alert("첨부 결과 확인", "\(errors.count)개 항목을 확인해 주세요. 다른 파일은 정상 첨부돼요.", details: errors.joined(separator: "\n\n"), style: .warning) }
        if !layoutWarnings.isEmpty {
            UI.alert("업체 양식이 바뀌었을 수 있어요", "파일은 첨부했지만 일부 열의 값이 평소와 달라요. 업체 사이트에서 받은 원본 시트인지, 열이 추가·이동되지 않았는지 확인한 뒤 조회해 주세요.",
                     details: layoutWarnings.joined(separator: "\n\n"), style: .warning)
        }
    }

    func removeFile(_ f: VendorFile) { files.removeAll { $0.id == f.id } }

    func cleanInput() {
        guard let p = parsed else { return }
        if !p.invalid.isEmpty {
            UI.alert("입력 확인", "인식하지 못한 \(p.invalid.count)줄은 정리할 때 빠져요. 필요한 줄이면 고쳐 주세요.", details: p.invalid.prefix(30).joined(separator: "\n"))
        }
        input = p.cleaned
    }

    // MARK: 조회

    var canLookup: Bool { !busy && (parsed?.vehicles.isEmpty == false) && !files.isEmpty }

    func lookup() {
        guard !busy else { return }
        let tidy = Self.dropBlankLines(input).trimmingCharacters(in: .newlines)
        if tidy != input { input = tidy }
        guard let p = try? rules.parseInput(input) else { return }
        if !p.invalid.isEmpty {
            UI.alert("차량목록을 확인해 주세요", "인식하지 못한 \(p.invalid.count)줄이 있어요. 아래 줄을 수정한 뒤 다시 조회해 주세요.", details: p.invalid.prefix(30).joined(separator: "\n"), style: .warning)
            return
        }
        guard !p.vehicles.isEmpty, !files.isEmpty else { toaster.show("차량목록과 업체 파일을 모두 넣어 주세요."); return }
        do {
            let rows = try rules.match(vehicles: p.vehicles, files: files.map(\.result))
            results = rows.enumerated().map { DRow(id: $0.offset, raw: $0.element) }
            selection = []; stale = false; slackText = ""; slackReport = ""
            if filter == "slack-replies" { filter = "all" }  // 다시 조회하면 회신 반영 보기는 사라져요
            let unique = Set(results.map(\.plate)).count
            status = "조회 완료 · 차량번호 \(unique)대 · 입력 중복 \(p.duplicates)건"
            toaster.show("차량 조회를 마쳤어요.")
        } catch {
            status = "조회 오류: \(error.localizedDescription)"
            UI.alert("차량 조회 오류", error.localizedDescription, style: .warning)
        }
    }

    // MARK: 표시

    var stateOptions: [String] { Array(Set(results.flatMap(\.rawStates))).sorted { $0.localizedCompare($1) == .orderedAscending } }
    var vendorOptions: [String] { Array(Set(results.map(\.vendor).filter { !$0.isEmpty })).sorted() }

    var visible: [DRow] {
        let q = search.replacingOccurrences(of: " ", with: "")
        var rows = results.filter { r in
            r.filters.contains(filter)
            && (q.isEmpty || r.plate.contains(q))
            && (vendorFilter == "all" || r.vendor == vendorFilter)
            && (stateFilter == "all" || r.rawStates.contains(stateFilter))
        }
        if !sortOrder.isEmpty { rows.sort(using: sortOrder) }
        return rows
    }

    func count(_ f: String) -> Int { Set(results.filter { $0.filters.contains(f) }.map(\.plate)).count }
    var distinctCount: Int { Set(results.map(\.plate)).count }

    var picked: [DRow]? { selection.isEmpty ? nil : results.filter { selection.contains($0.id) } }

    var scopeRows: [JSONObject] {
        let rows = picked ?? (scope == "confirmed" ? results.filter { $0.status == "confirmed" } : scope == "visible" ? visible : results)
        return rules.unique(rows.map(\.raw))
    }
    var scopeLabel: String { picked != nil ? "선택한 줄" : (Self.scopes.first { $0.0 == scope }?.1 ?? "") }

    // MARK: 고르기·완료

    func choose(_ row: DRow, field: String, id: String) {
        guard let i = results.firstIndex(where: { $0.id == row.id }) else { return }
        do { results[i].raw = try rules.useCandidate(results[i].raw, field: field, id: id) }
        catch { UI.alert("선택하지 못했어요", error.localizedDescription) }
    }

    func toggleDone(_ plate: String) {
        if completed.contains(plate) { completed.remove(plate) } else { completed.insert(plate) }
    }

    // MARK: 복사·저장

    func copyPlates() {
        let rows = rules.unique((picked ?? visible).map(\.raw))
        guard !rows.isEmpty else { toaster.show("현재 목록에 복사할 차량이 없어요."); return }
        let plates = rows.compactMap { $0["plate"] as? String }
        let text: String
        if filter == "slack" {
            let jeju = rows.filter { $0["jeju"] as? Bool == true }.compactMap { $0["plate"] as? String }
            let normal = rows.filter { $0["jeju"] as? Bool != true }.compactMap { $0["plate"] as? String }
            text = [("제주도출발차량", jeju), ("일반차량", normal)].filter { !$0.1.isEmpty }
                .map { "[\($0.0)]\n" + $0.1.joined(separator: "\n") }.joined(separator: "\n\n")
        } else {
            text = plates.joined(separator: "\n")
        }
        UI.copy(text)
        copied.formUnion(plates)
        toaster.show(UI.copyPreview(text))
    }

    func copyCell(_ label: String, _ value: String) {
        guard !value.isEmpty else { return }
        UI.copy(value)
        toaster.show("\(label) 복사: \(value)")
    }

    private func guardExport(_ rows: [JSONObject]) -> Bool {
        guard !results.isEmpty else { toaster.show("먼저 차량 조회를 해 주세요."); return false }
        guard !rows.isEmpty else { toaster.show("선택한 범위에 차량이 없어요."); return false }
        let pending = rows.filter { $0["_unresolved"] as? Bool == true }
        if !pending.isEmpty {
            let lines = pending.prefix(25).map { r in
                let plate = r["plate"] as? String ?? ""
                let what = [(r["scheduleConflict"] as? Bool == true) ? "일정" : "", (r["feeConflict"] as? Bool == true) ? "탁송비용" : ""].filter { !$0.isEmpty }.joined(separator: ", ")
                return "\(plate) · \(what)"
            }
            UI.alert("중첩 일정·탁송비용 선택 필요", "\(pending.count)대의 일정 또는 탁송비용을 먼저 선택해 주세요. 선택하지 않은 값을 임의로 저장하지 않아요.", details: lines.joined(separator: "\n"), style: .warning)
            return false
        }
        return true
    }

    func capture(copy: Bool) {
        let rows = scopeRows
        guard !results.isEmpty else { toaster.show("먼저 차량 조회를 해 주세요."); return }
        guard !rows.isEmpty else { toaster.show("선택한 범위에 차량이 없어요."); return }
        if rows.count > DispatchExports.captureMax {
            UI.alert("캡처할 차량이 너무 많아요", "이미지 한 장에는 \(DispatchExports.captureMax)대까지 담을 수 있어요. 저장 범위를 좁히거나 필요한 줄만 선택해 주세요. 많은 양은 엑셀·PDF 저장을 써 주세요.")
            return
        }
        let png = DispatchExports.capturePNG(rows, scopeLabel: scopeLabel)
        if copy {
            UI.copyImage(png)
            toaster.show("결과 이미지를 복사했어요. Slack에 붙여넣으세요.")
        } else if UI.save(png, name: "탁송조회_\(UI.stamp("yyyyMMdd_HHmmss")).png", types: [.png]) != nil {
            toaster.show("결과 이미지를 저장했어요.")
        }
    }

    func export(_ type: String) {
        let rows = scopeRows
        guard guardExport(rows) else { return }
        let name = "탁송조회_\(UI.stamp("yyyyMMdd_HHmmss")).\(type)"
        Task {
            busy = true; status = "\(type.uppercased()) 파일 만드는 중…"
            defer { busy = false }
            do {
                let data: Data
                let types: [UTType]
                switch type {
                case "txt": data = Data(try rules.textExport(rows).utf8); types = [.plainText]
                case "xlsx": data = try await rules.excelExport(rows); types = [UTType(filenameExtension: "xlsx")!]
                default: data = DispatchExports.pdf(rows); types = [.pdf]
                }
                if UI.save(data, name: name, types: types) != nil {
                    status = "\(rows.count)대 · \(type.uppercased()) 저장 완료"; toaster.show("결과 파일을 저장했어요.")
                } else { status = "파일 저장을 취소했어요." }
            } catch {
                UI.alert("파일을 저장하지 못했어요", error.localizedDescription, style: .warning)
            }
        }
    }

    // MARK: 슬랙봇 회신

    var hasSlackTargets: Bool { results.contains { $0.flag("cInquiry") } }

    func applySlack() {
        guard !slackText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { slackReport = "슬랙봇 회신을 붙여넣어 주세요."; return }
        do {
            let r = try rules.applySlackReplies(slackText, rows: results.map(\.raw))
            results = r.rows.enumerated().map { DRow(id: $0.offset, raw: $0.element) }
            slackReport = (["\(r.applied)대 반영 · 확인 필요 \(r.issues.count)건"] + r.issues.prefix(8) + (r.issues.count > 8 ? ["외 \(r.issues.count - 8)건"] : [])).joined(separator: "\n")
            if r.applied > 0 && r.issues.isEmpty { viewSlackReplies(); toaster.show("슬랙봇 회신 \(r.applied)대를 표에 반영했어요.") }
        } catch {
            slackReport = error.localizedDescription
        }
    }

    var hasSlackReplies: Bool { results.contains { $0.flag("slackReviewed") } }

    func viewSlackReplies() {
        showSlack = false
        search = ""; vendorFilter = "all"; stateFilter = "all"; scope = "visible"; selection = []
        filter = "slack-replies"
    }

    func reset() {
        input = ""; files = []; results = []; selection = []; completed = []; copied = []
        filter = "all"; search = ""; stale = false; slackText = ""; slackReport = ""
        status = "차량목록과 업체 파일을 넣고 조회해 주세요."
    }
}
