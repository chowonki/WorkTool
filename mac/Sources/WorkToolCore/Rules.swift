import Foundation

public typealias JSONObject = [String: Any]

/// 웹 WorkTool과 같은 규칙(탁송체크·이체파일)을 부르는 창구예요.
public final class Rules: @unchecked Sendable {
    public static let shared: Rules = {
        do { return try Rules() } catch { fatalError("규칙을 불러오지 못했어요: \(error)") }
    }()

    let engine: JSEngine

    public init() throws {
        engine = try JSEngine(scripts: [
            ("excel.js", AppResources.text("js/excel.js")),
            ("dispatch.js", AppResources.text("js/dispatch.js")),
            ("transfer.js", AppResources.text("js/transfer.js")),
        ])
    }

    // MARK: 탁송체크

    /// 탁송 필터 키 (웹 DispatchCore.includes와 같아요)
    public static let dispatchFilters = ["all", "review", "confirmed", "progress", "missing", "jeju", "carrier", "slack", "slack-replies"]

    public struct ParsedInput {
        public let vehicles: [JSONObject]
        public let invalid: [String]
        public let duplicates: Int
        public let cleaned: String
    }

    public func parseInput(_ text: String) throws -> ParsedInput {
        try engine.runSync { e in
            try e.set("__in", text)
            let r = try e.json("DispatchCore.parseInput(__in)") as? JSONObject ?? [:]
            return ParsedInput(vehicles: r["vehicles"] as? [JSONObject] ?? [],
                               invalid: r["invalid"] as? [String] ?? [],
                               duplicates: (r["duplicates"] as? NSNumber)?.intValue ?? 0,
                               cleaned: r["cleaned"] as? String ?? "")
        }
    }

    /// 업체 엑셀 한 개를 읽어요. 암호 파일이면 password가 필요해요. 결과는 {name,records,sheets,ignored,warnings}.
    public func readVendorFile(_ data: Data, name: String, password: String?) async throws -> JSONObject {
        try await engine.run { e in
            e.setBytes("__file", data)
            e.setString("__name", name)
            e.setString("__pw", password)
            do {
                _ = try e.await("(async()=>{const b=__pw!=null?await DispatchExcel.decrypt(__file.buffer,__pw):__file.buffer;const sheets=await DispatchExcel.readAsync(b);globalThis.__read=DispatchCore.readSheets(sheets,__name);})()")
            } catch {
                e.setString("__pw", nil)
                if password != nil { throw EngineError("암호가 맞지 않거나 지원하지 않는 암호화 파일이에요. 암호와 파일을 확인해 주세요.") }
                throw error
            }
            e.setString("__pw", nil)
            e.setBytes("__file", Data())
            return try e.json("__read") as? JSONObject ?? [:]
        }
    }

    public static func isEncryptedExcel(_ data: Data) -> Bool {
        data.prefix(8).elementsEqual([0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1])
    }

    private static let annotate = """
    rs=>rs.map(r=>Object.assign(r,{_label:DispatchCore.statusLabel(r)||'',_notes:DispatchCore.notes(r),_progress:DispatchCore.inProgress(r),\
    _unresolved:DispatchCore.hasUnresolvedChoice(r),_filters:\(dispatchFiltersJS).filter(f=>DispatchCore.includes(r,f))}))
    """
    private static var dispatchFiltersJS: String { "['" + dispatchFilters.joined(separator: "','") + "']" }

    public func match(vehicles: [JSONObject], files: [JSONObject]) throws -> [JSONObject] {
        try engine.runSync { e in
            try e.set("__v", vehicles); try e.set("__f", files)
            return try e.json("(\(Self.annotate))(DispatchCore.match(__v,__f))") as? [JSONObject] ?? []
        }
    }

    public func annotate(_ rows: [JSONObject]) throws -> [JSONObject] {
        try engine.runSync { e in
            try e.set("__rows", rows)
            return try e.json("(\(Self.annotate))(__rows)") as? [JSONObject] ?? []
        }
    }

    /// 중첩된 탁송일정·탁송비용 중 하나를 골라요. field는 "schedule" 또는 "fee".
    public func useCandidate(_ row: JSONObject, field: String, id: String) throws -> JSONObject {
        try engine.runSync { e in
            try e.set("__row", row); try e.set("__field", field); try e.set("__id", id)
            return try e.json("(DispatchCore.useCandidate(__row,__field,__id),(\(Self.annotate))([__row])[0])") as? JSONObject ?? row
        }
    }

    public func applySlackReplies(_ text: String, rows: [JSONObject]) throws -> (rows: [JSONObject], applied: Int, issues: [String]) {
        try engine.runSync { e in
            try e.set("__rows", rows); try e.set("__text", text)
            let r = try e.json("(()=>{const x=DispatchCore.applySlackReplies(__text,__rows);return {rows:(\(Self.annotate))(__rows),...x}})()") as? JSONObject ?? [:]
            return (r["rows"] as? [JSONObject] ?? rows, (r["applied"] as? NSNumber)?.intValue ?? 0, r["issues"] as? [String] ?? [])
        }
    }

    public func unique(_ rows: [JSONObject]) -> [JSONObject] {
        var seen = Set<String>(); var order: [String] = []; var last: [String: JSONObject] = [:]
        for r in rows { let p = r["plate"] as? String ?? ""; if seen.insert(p).inserted { order.append(p) }; last[p] = r }
        return order.compactMap { last[$0] }
    }

    public func textExport(_ rows: [JSONObject]) throws -> String {
        try engine.runSync { e in try e.set("__rows", rows); return try e.string("DispatchCore.textExport(__rows)") }
    }

    public func excelExport(_ rows: [JSONObject]) async throws -> Data {
        try await engine.run { e in
            try e.set("__rows", rows)
            _ = try e.await("DispatchExport.excelBlob(__rows)")
            return try e.bytes("__result")
        }
    }

    // MARK: 이체파일

    public struct BankGroup: Identifiable { public let id: String; public let banks: [String] }
    public lazy var bankGroups: [BankGroup] = {
        let g = ((try? engine.runSync { e -> Any in try e.json("Transfer.banks") }) as? [[Any]]) ?? []
        return g.map { BankGroup(id: $0[0] as? String ?? "", banks: $0[1] as? [String] ?? []) }
    }()
    public lazy var bankCodes: [String: String] = ((try? engine.runSync { e -> Any in try e.json("Transfer.codes") }) as? [String: String]) ?? [:]
    public let transferMax = 100

    public func transferCall(_ expression: String, rows: [JSONObject], extra: [String: Any] = [:]) throws -> Any {
        try engine.runSync { e in
            try e.set("__rows", rows)
            for (k, v) in extra { try e.set(k, v) }
            return try e.json(expression)
        }
    }

    public func transferXlsx(_ rows: [JSONObject]) throws -> Data {
        try engine.runSync { e in try e.set("__rows", rows); return try e.bytes("Transfer.xlsx(__rows)") }
    }
}
