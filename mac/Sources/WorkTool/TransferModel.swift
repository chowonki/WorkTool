import SwiftUI
import UniformTypeIdentifiers
import WorkToolCore

/// 이체 한 건 (웹 transfer.html의 rows 항목과 같은 모양)
struct TRow: Codable, Identifiable, Equatable {
    var id: String
    var bank = ""
    var account = ""
    var amount = ""
    var plate = ""
    var outMemo = ""
    var inMemo = ""
    var cms = ""
    var holder: String? = ""
    var source: String? = ""
    var ctx: String? = nil
    var label: String? = nil
    var thread: Int? = 0
    var threadId: String? = nil
    var srcKey: String? = nil
    var ambiguous: Bool? = false
    var splitGroup: String? = nil
}

struct TThread: Identifiable { let id: String; var text = ""; var imported = "" }

struct TCheck { var state = "ok"; var errors: [String] = []; var reviews: [String] = [] }

@MainActor
final class TransferModel: ObservableObject {
    let rules = Rules.shared
    let toaster = Toaster()

    @Published var step = 1
    @Published var threads: [TThread] = []
    @Published var rows: [TRow] = [] { didSet { recheck() } }
    @Published private(set) var checks: [String: TCheck] = [:]
    @Published private(set) var splitLabels: [String: String] = [:]
    @Published var filter = "all"
    @Published var selection = Set<String>()
    @Published var cmsAll = ""
    @Published var outMemoAll = ""
    @Published var inMemoAll = ""
    @Published var onlyEmpty = true
    @Published var autoFix = false { didSet { if autoFix { rows = rows.map { var r = $0; r.account = r.account.filter { !" -".contains($0) }; return r } } } }
    @Published var extractInfo = ""
    @Published var extractedAt = ""
    @Published var ack = false
    @Published var askPlates: [TRow] = []
    @Published var askWarnings: [String] = []
    private var ackSig = ""
    private(set) var downloadedSig = ""

    init() { threads = [TThread(id: newId())] }

    func newId() -> String { (try? rules.transferCall("Transfer.newId()", rows: []) as? String) ?? UUID().uuidString }

    private func json(_ list: [TRow]) -> [JSONObject] {
        guard let d = try? JSONEncoder().encode(list), let a = try? JSONSerialization.jsonObject(with: d) as? [JSONObject] else { return [] }
        return a
    }
    private func decode(_ list: Any?) -> [TRow] {
        guard let list, let d = try? JSONSerialization.data(withJSONObject: list) else { return [] }
        return (try? JSONDecoder().decode([TRow].self, from: d)) ?? []
    }

    private func recheck() {
        let js = json(rows)
        let res = (try? rules.transferCall("Transfer.check(__rows)", rows: js) as? [JSONObject]) ?? []
        let labels = (try? rules.transferCall("Transfer.splitLabel(__rows)", rows: js) as? [String]) ?? []
        var c: [String: TCheck] = [:], l: [String: String] = [:]
        for (i, r) in rows.enumerated() {
            if i < res.count {
                c[r.id] = TCheck(state: res[i]["state"] as? String ?? "ok", errors: res[i]["errors"] as? [String] ?? [], reviews: res[i]["reviews"] as? [String] ?? [])
            }
            if i < labels.count, !labels[i].isEmpty { l[r.id] = labels[i] }
        }
        checks = c; splitLabels = l
        if ack && ackSig != dataSig { ack = false; ackSig = "" }
    }

    // MARK: 통계

    struct Counts { var all = 0, ok = 0, warn = 0, bad = 0; var sum: Double = 0 }
    var counts: Counts {
        var c = Counts(all: rows.count)
        for r in rows {
            switch checks[r.id]?.state ?? "ok" { case "bad": c.bad += 1; case "warn": c.warn += 1; default: c.ok += 1 }
            if let a = Double(r.amount), a > 0, a == a.rounded() { c.sum += a }
        }
        return c
    }

    var visibleRows: [TRow] { filter == "all" ? rows : rows.filter { (checks[$0.id]?.state ?? "ok") == filter } }

    var dataSig: String { rows.map { [$0.bank, $0.account, $0.amount, $0.outMemo, $0.inMemo, $0.cms, $0.plate].joined(separator: "\u{1f}") }.joined(separator: "\u{1e}") }
    var hasUnsaved: Bool { !rows.isEmpty && downloadedSig != dataSig }

    func setAck(_ on: Bool) { ack = on; ackSig = on ? dataSig : "" }

    // MARK: 1단계: 추출

    func addThread() { threads.append(TThread(id: newId())) }
    func removeThread(_ id: String) { if threads.count > 1 { threads.removeAll { $0.id == id } } else { threads[0].text = "" } }

    func extract() {
        guard threads.contains(where: { !$0.text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty }) else { toaster.show("스레드 내용을 먼저 붙여넣어 주세요."); return }
        let pending = threads.indices.filter { let raw = trimmed(threads[$0].text); return !raw.isEmpty && threads[$0].imported != raw }
        guard !pending.isEmpty else { toaster.show("새로 넣거나 바뀐 스레드가 없어요. 입력칸을 추가해 새 내용을 넣어 주세요."); return }
        let prevImported = pending.map { threads[$0].imported }
        var added: [JSONObject] = [], warnings: [String] = [], notes: [String] = []
        var seenRaw: [String: Int] = [:]
        for (i, t) in threads.enumerated() { let r = trimmed(t.text); if !t.imported.isEmpty && t.imported == r && seenRaw[r] == nil { seenRaw[r] = i + 1 } }
        for i in pending {
            let raw = trimmed(threads[i].text)
            if let same = seenRaw[raw] { warnings.append("이체 요청 \(i + 1): 이체 요청 \(same)와 내용이 같아 건너뛰었어요."); threads[i].imported = raw; continue }
            seenRaw[raw] = i + 1
            let res = (try? rules.transferCall("(Transfer.setRows(__rows),Transfer.extractThread(__raw,__i,__tid,__cms))", rows: json(rows),
                                               extra: ["__raw": raw, "__i": i, "__tid": threads[i].id, "__cms": cmsAll.trimmingCharacters(in: .whitespaces)]) as? JSONObject) ?? [:]
            if res["empty"] as? Bool == true { warnings.append("이체 요청 \(i + 1): 이체정보를 찾지 못했어요."); continue }
            added += res["added"] as? [JSONObject] ?? []
            let skipped = (res["skipped"] as? NSNumber)?.intValue ?? 0, fresh = (res["fresh"] as? NSNumber)?.intValue ?? 0
            if skipped > 0 { notes.append("이체 요청 \(i + 1): 이미 추출한 \(skipped)건은 다시 넣지 않았어요\(fresh > 0 ? " (새 \(fresh)건 추가)" : "").") }
            threads[i].imported = raw
        }
        let before = added.count
        let split = decode(try? rules.transferCall("Transfer.splitAll(__rows)", rows: added))
        let nSplit = split.count - before
        if split.isEmpty {
            UI.alert("추출 결과", (notes + warnings).joined(separator: "\n").isEmpty ? "추출할 이체정보가 없어요." : (notes + warnings).joined(separator: "\n"))
            return
        }
        if rows.count + split.count > rules.transferMax {
            UI.alert("최대 건수 초과", "기존 \(rows.count)건 + 새 \(split.count)건 = \(rows.count + split.count)건이에요.\n한 파일에 \(rules.transferMax)건까지 만들 수 있어요. 나눠서 처리해 주세요.")
            for (k, i) in pending.enumerated() { threads[i].imported = prevImported[k] }
            return
        }
        rows.append(contentsOf: split)
        extractedAt = UI.stamp("yyyy. M. d. HH:mm")
        extractInfo = "새 \(split.count)건 추가 · 총 \(rows.count)건"
        step = 2
        let noPlate = split.filter { r in r.plate.isEmpty && (r.splitGroup == nil || split.first { $0.splitGroup == r.splitGroup }?.id == r.id) }
        if !noPlate.isEmpty { askWarnings = warnings; askPlates = noPlate }
        else if !warnings.isEmpty { UI.alert("확인해 주세요", warnings.joined(separator: "\n")) }
        else { toaster.show(notes.isEmpty ? "\(split.count)건을 추출했어요. 원문과 맞는지 확인해 주세요." : notes.joined(separator: " ")) }
        if nSplit > 0 { Task { try? await Task.sleep(for: .seconds(3)); toaster.show("1억 넘는 금액을 나눠 \(nSplit)줄을 더 만들었어요. ‘분할’ 표시를 확인해 주세요.") } }
    }

    private func trimmed(_ s: String) -> String { s.replacingOccurrences(of: "\r\n", with: "\n").replacingOccurrences(of: "\r", with: "\n").trimmingCharacters(in: .whitespacesAndNewlines) }

    func groupIds(_ id: String) -> [String] {
        guard let r = rows.first(where: { $0.id == id }) else { return [] }
        guard let g = r.splitGroup else { return [id] }
        return rows.filter { $0.splitGroup == g }.map(\.id)
    }

    func isPlate(_ v: String) -> Bool { (try? rules.transferCall("Transfer.plateOK(__v)", rows: [], extra: ["__v": v]) as? Bool) ?? false }

    /// 차량번호가 없던 건에 입력한 값을 반영해요. 형식이 틀리면 그 차량번호를 돌려줘요.
    func applyPlates(_ inputs: [(id: String, usePlate: Bool, text: String)]) -> String? {
        for x in inputs where x.usePlate {
            let v = x.text.filter { !$0.isWhitespace }
            if !v.isEmpty && !isPlate(v) { return v }
        }
        for x in inputs {
            let ids = Set(groupIds(x.id))
            if x.usePlate {
                let v = x.text.filter { !$0.isWhitespace }
                guard !v.isEmpty else { continue }
                for i in rows.indices where ids.contains(rows[i].id) {
                    rows[i].plate = v
                    if rows[i].outMemo.isEmpty { rows[i].outMemo = v }
                    if rows[i].inMemo.isEmpty { rows[i].inMemo = v }
                }
            } else {
                let v = x.text.trimmingCharacters(in: .whitespaces)
                guard !v.isEmpty else { continue }
                for i in rows.indices where ids.contains(rows[i].id) {
                    rows[i].inMemo = v
                    if rows[i].outMemo.isEmpty { rows[i].outMemo = v }
                }
            }
        }
        askPlates = []
        toaster.show("입력한 내용을 반영했어요.")
        return nil
    }

    // MARK: 2단계: 확인·수정

    func binding(_ id: String, _ key: WritableKeyPath<TRow, String>) -> Binding<String> {
        Binding(get: { [weak self] in self?.rows.first { $0.id == id }?[keyPath: key] ?? "" },
                set: { [weak self] v in
                    guard let self, let i = self.rows.firstIndex(where: { $0.id == id }) else { return }
                    var value = v
                    if key == \TRow.account && self.autoFix { value = value.filter { !" -".contains($0) } }
                    if key == \TRow.plate {
                        let old = self.rows[i].plate
                        if self.rows[i].outMemo.isEmpty || self.rows[i].outMemo == old { self.rows[i].outMemo = value }
                        if self.rows[i].inMemo.isEmpty || self.rows[i].inMemo == old { self.rows[i].inMemo = value }
                    }
                    self.rows[i][keyPath: key] = value
                })
    }

    /// 입금액: 화면에는 쉼표를 넣고, 값에는 숫자만 남겨요.
    func amountBinding(_ id: String) -> Binding<String> {
        Binding(get: { [weak self] in
                    let a = self?.rows.first { $0.id == id }?.amount ?? ""
                    if let n = Double(a), a.allSatisfy(\.isNumber), !a.isEmpty { return UI.won(n) }
                    return a
                },
                set: { [weak self] v in
                    guard let self, let i = self.rows.firstIndex(where: { $0.id == id }) else { return }
                    self.rows[i].amount = v.replacingOccurrences(of: ",", with: "").replacingOccurrences(of: "원", with: "").trimmingCharacters(in: .whitespaces)
                })
    }

    func addRow() {
        guard rows.count < rules.transferMax else { toaster.show("최대 \(rules.transferMax)건까지예요."); return }
        let r = TRow(id: newId(), cms: cmsAll.trimmingCharacters(in: .whitespaces))
        filter = "all"
        rows.append(r)
        selection = [r.id]
    }

    func deleteSelected() {
        guard !selection.isEmpty else { toaster.show("삭제할 줄을 선택해 주세요."); return }
        let n = selection.count
        guard UI.confirm("선택한 \(n)건을 삭제할까요?", "삭제한 줄은 되돌릴 수 없어요.", ok: "삭제", destructive: true) else { return }
        rows.removeAll { selection.contains($0.id) }
        selection = []
        toaster.show("\(n)건을 삭제했어요.")
    }

    func applyMemo(_ key: WritableKeyPath<TRow, String>, _ value: String, title: String) {
        guard !rows.isEmpty else { toaster.show("적용할 이체 내역이 없어요."); return }
        var n = 0
        for i in rows.indices where !onlyEmpty || rows[i][keyPath: key].isEmpty { rows[i][keyPath: key] = value; n += 1 }
        toaster.show("\(title)을(를) \(n)건에 적용했어요.")
    }

    func goStep3() {
        let c = counts
        guard c.all > 0 else { toaster.show("이체 내역이 없어요."); return }
        step = 3
        if c.bad > 0 { toaster.show("오류 \(c.bad)건이 남아 있어요. 고친 뒤 받을 수 있어요.") }
    }

    // MARK: 3단계: 다운로드

    var fileRows: [[Any]] { (try? rules.transferCall("Transfer.fileRows(__rows)", rows: json(rows)) as? [[Any]]) ?? [] }
    var comments: [String] { (try? rules.transferCall("Transfer.comments(__rows)", rows: json(rows)) as? [String]) ?? [] }

    var canDownload: Bool { let c = counts; return c.all > 0 && c.bad == 0 && c.all <= rules.transferMax && (c.warn == 0 || ack) }

    func download() {
        guard canDownload else { return }
        let js = json(rows)
        let bad = rows.filter { checks[$0.id]?.state == "bad" }
        if !bad.isEmpty { toaster.show("오류 \(bad.count)건이 남아 있어요. 2단계에서 고쳐 주세요."); return }
        let keys = (try? rules.transferCall("Transfer.sentKeys(__rows)", rows: js) as? [String]) ?? []
        let sent = SentLog.load()
        let again = keys.enumerated().compactMap { k in sent[k.element].map { (row: k.offset, at: $0) } }
        if !again.isEmpty && downloadedSig != dataSig {
            var lines = again.prefix(10).map { m in let r = rows[m.row]; return "\(r.bank) \(r.account) \(UI.won(Double(r.amount) ?? 0))원 · \(m.at)에 받음" }
            if again.count > 10 { lines.append("외 \(again.count - 10)건") }
            guard UI.confirm("오늘 이미 받은 이체 건이 있어요", "같은 은행·계좌·금액 \(again.count)건이 오늘 받은 이체파일에 이미 들어 있어요. 두 번 송금하지 않도록 확인해 주세요.",
                             ok: "그래도 받기", details: lines.joined(separator: "\n")) else { return }
        }
        let now = Date(), stamp = UI.stamp("yyyyMMdd_HHmm", now)
        do {
            let name = (try rules.transferCall("Transfer.fileName(__rows,__stamp)", rows: js, extra: ["__stamp": stamp]) as? String) ?? "하나은행_다계좌이체_\(stamp).xlsx"
            let data = try rules.transferXlsx(js)
            guard UI.save(data, name: name, types: [UTType(filenameExtension: "xlsx")!]) != nil else { return }
            downloadedSig = dataSig
            SentLog.remember(keys, at: UI.stamp("HH:mm", now))
            objectWillChange.send()
            toaster.show("이체파일을 저장했어요. 은행 업로드 전에 한 번 더 확인해 주세요.")
        } catch {
            UI.alert("파일을 만들지 못했어요", "입력 내용은 그대로 남아 있어요. 잠시 뒤 다시 눌러 주세요.\n\(error.localizedDescription)", style: .warning)
        }
    }

    func copyComments(_ lines: [String]) {
        guard !lines.isEmpty else { toaster.show("이체 내역이 없어요."); return }
        UI.copy(lines.joined(separator: "\n"))
        toaster.show(lines.count == 1 ? "코멘트 한 줄을 복사했어요." : "송금 코멘트 \(lines.count)줄을 복사했어요.")
    }

    func resetAll() {
        if hasUnsaved && !UI.confirm("처음부터 다시 할까요?", "받지 않은 이체 내역 \(rows.count)건이 지워져요.", ok: "지우기", destructive: true) { return }
        rows = []; threads = [TThread(id: newId())]; step = 1; filter = "all"; selection = []
        extractInfo = ""; extractedAt = ""; ack = false; ackSig = ""; downloadedSig = ""
    }
}

/// 오늘 받은 이체 지문(은행·계좌·금액을 섞은 값). 계좌번호 원문은 남기지 않아요. 날짜가 바뀌면 지워요.
enum SentLog {
    static let key = "transfer-sent-today-v1"
    static func today() -> String { UI.stamp("yyyy-MM-dd") }
    static func load() -> [String: String] {
        guard let d = UserDefaults.standard.dictionary(forKey: key), d["date"] as? String == today(),
              let items = d["items"] as? [[String: String]] else { UserDefaults.standard.removeObject(forKey: key); return [:] }
        var m: [String: String] = [:]
        for x in items { if let k = x["key"] { m[k] = x["at"] ?? "" } }
        return m
    }
    static func remember(_ keys: [String], at: String) {
        var items = (UserDefaults.standard.dictionary(forKey: key)?["date"] as? String == today()
                     ? UserDefaults.standard.dictionary(forKey: key)?["items"] as? [[String: String]] : nil) ?? []
        var have = Set(items.compactMap { $0["key"] })
        for k in keys where have.insert(k).inserted { items.append(["key": k, "at": at]) }
        UserDefaults.standard.set(["date": today(), "items": Array(items.suffix(2000))], forKey: key)
    }
}
