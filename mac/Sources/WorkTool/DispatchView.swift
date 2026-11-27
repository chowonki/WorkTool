import SwiftUI
import UniformTypeIdentifiers
import WorkToolCore

struct DispatchView: View {
    @ObservedObject var model: DispatchModel
    @AppStorage("dispatch-inspector") private var showInspector = true

    var body: some View {
        VStack(spacing: 0) {
            scopeBar
            if model.results.isEmpty {
                ContentUnavailableView {
                    Label("조회할 차량목록을 준비해 주세요", systemImage: "car.2")
                } description: {
                    Text("오른쪽 입력 패널에 차량목록을 붙여넣고 업체 파일을 첨부한 뒤\n차량 조회하기(⌘↩)를 누르세요.")
                } actions: {
                    if !showInspector { Button("입력 패널 열기") { showInspector = true } }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                DispatchTable(model: model)
            }
            FooterBar {
                Text(model.status).lineLimit(1)
                Spacer()
                if let picked = model.picked { Text("선택 \(Set(picked.map(\.plate)).count)대").foregroundStyle(Theme.accent) }
                if !model.results.isEmpty { Text("\(model.visible.count)줄 · \(Set(model.visible.map(\.plate)).count)대") }
            }
        }
        .navigationTitle("탁송체크")
        .navigationSubtitle(subtitle)
        .searchable(text: $model.search, placement: .toolbar, prompt: "차량번호 검색")
        .toolbar { toolbar }
        .inspector(isPresented: $showInspector) {
            DispatchInspector(model: model)
                .inspectorColumnWidth(min: 270, ideal: 310, max: 440)
        }
        .overlay(ToastOverlay(toaster: model.toaster))
        .sheet(isPresented: $model.showSlack) { SlackRepliesSheet(model: model) }
        .background {
            // 입력 패널을 숨겨도 ⌘↩로 조회해요. 조회하면서 패널을 다시 보여 줘요.
            Button("차량 조회하기") { showInspector = true; model.lookup() }
                .keyboardShortcut(.return, modifiers: .command)
                .disabled(!model.canLookup)
                .opacity(0).frame(width: 0, height: 0).accessibilityHidden(true)
        }
    }

    private var subtitle: String {
        let title = DispatchModel.filterTitles.first { $0.0 == model.filter }?.1 ?? "전체"
        if model.results.isEmpty { return "차량목록과 업체 파일을 넣고 조회해 주세요" }
        return "\(title) · \(model.filter == "all" ? model.distinctCount : model.count(model.filter))대" + (model.stale ? " · 입력이 바뀌었어요" : "")
    }

    // MARK: 도구 막대

    @ToolbarContentBuilder private var toolbar: some ToolbarContent {
        ToolbarItemGroup(placement: .primaryAction) {
            Group {
                Button { model.copyPlates() } label: { Label { Text("차량번호 복사") } icon: { Image(systemName: "doc.on.doc.fill").foregroundStyle(Theme.teal) } }
                    .help("선택한 줄(없으면 현재 목록)의 차량번호를 복사해요. 슬랙봇 문의 보기에서는 제주·일반으로 나눠요.")
                Menu {
                    Button("클립보드에 복사 (Slack 붙여넣기)") { model.capture(copy: true) }
                    Button("PNG로 저장…") { model.capture(copy: false) }
                } label: { Label { Text("캡처") } icon: { Image(systemName: "camera.fill").foregroundStyle(Theme.indigo) } } primaryAction: { model.capture(copy: true) }
                    .help("누르면 결과 이미지를 클립보드에 복사해요. ▾에서 PNG 저장")
                Menu {
                    Button("엑셀(.xlsx)…") { model.export("xlsx") }
                    Button("PDF…") { model.export("pdf") }
                    Button("TXT…") { model.export("txt") }
                } label: { Label { Text("저장") } icon: { Image(systemName: "square.and.arrow.down.fill").foregroundStyle(Theme.info) } }
                Button { model.showSlack = true } label: { Label { Text("슬랙봇 회신") } icon: { Image(systemName: "bubble.left.and.text.bubble.right.fill").foregroundStyle(Theme.purple) } }
                    .disabled(!model.hasSlackTargets)
            }
            .labelStyle(.titleAndIcon)
            .disabled(model.results.isEmpty || model.busy)
        }
        ToolbarItem(placement: .primaryAction) {
            Button { showInspector.toggle() } label: { Label("입력 패널", systemImage: "sidebar.trailing") }
                .help("차량목록·업체 파일 입력 패널 보이기/숨기기")
        }
    }

    // MARK: 범위 줄

    private var scopeBar: some View {
        ScopeBar {
            Picker("업체", selection: $model.vendorFilter) {
                Text("모든 업체").tag("all")
                ForEach(model.vendorOptions, id: \.self) { Text($0).tag($0) }
            }
            .fixedSize()
            Picker("상태", selection: $model.stateFilter) {
                Text("모든 상태").tag("all")
                ForEach(model.stateOptions, id: \.self) { Text($0).tag($0) }
            }
            .fixedSize()
            Spacer()
            Picker("저장 범위", selection: $model.scope) {
                ForEach(DispatchModel.scopes, id: \.0) { Text($0.1).tag($0.0) }
            }
            .fixedSize()
            .help("복사·캡처·저장할 범위예요. 줄을 선택하면 선택한 줄이 먼저예요.")
        }
        .disabled(model.results.isEmpty)
    }
}

/// 오른쪽 입력 패널: 차량목록 + 업체 파일 + 조회 버튼
struct DispatchInspector: View {
    @ObservedObject var model: DispatchModel
    @State private var dropTarget = false

    var body: some View {
        VStack(spacing: 0) {
            InspectorScroll(title: "입력", icon: "square.and.pencil", color: Theme.accent) {
                VStack(alignment: .leading, spacing: 18) {
                    VStack(alignment: .leading, spacing: 8) {
                        SectionHeader("차량목록", icon: "list.bullet", color: Theme.accent, trailing: AnyView(
                            Button("정리") { model.cleanInput() }.controlSize(.small).disabled(model.parsed == nil)
                                .help("날짜·시간을 맞추고 인식한 차량만 남겨요")))
                        TextEditor(text: $model.input)
                            .font(Theme.mono)
                            .scrollContentBackground(.hidden)
                            .padding(5)
                            .frame(height: 220)
                            .background(Color(nsColor: .textBackgroundColor), in: RoundedRectangle(cornerRadius: 6))
                            .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(Color.primary.opacity(0.12)))
                        inputSummary
                        Text("‘탁송출발 주유비 미입력’ 결과에서 차량번호를 복사해 붙여넣어요. 날짜·시간이 함께 있어도 돼요.")
                            .font(Theme.small).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                    }
                    Divider()
                    VStack(alignment: .leading, spacing: 8) {
                        SectionHeader("업체 파일", icon: "tablecells.fill", color: Theme.info, trailing: AnyView(
                            Button("파일 선택…") { model.pickFiles() }.controlSize(.small).disabled(model.busy)))
                        fileList
                        Text("한빛탁송·오픈마일·렌톡에서 ‘탁송기한’으로 조회해 받은 .xlsx를 넣어요. 렌톡은 출발시각 기준이에요.")
                            .font(Theme.small).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                    }
                }
                .padding(16)
            }
            Divider()
            VStack(spacing: 6) {
                if model.stale {
                    Label("입력이 바뀌었어요. 다시 조회해 주세요.", systemImage: "exclamationmark.circle")
                        .font(Theme.small).foregroundStyle(Theme.warn)
                }
                Button { model.lookup() } label: {
                    HStack(spacing: 6) {
                        if model.busy { ProgressView().controlSize(.small) }
                        Text("차량 조회하기").fontWeight(.semibold)
                        Text("⌘↩").foregroundStyle(.white.opacity(0.7))
                    }
                    .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent).controlSize(.large)
                .tint(Theme.accent)
                .disabled(!model.canLookup)
            }
            .padding(14)
        }
    }

    @ViewBuilder private var inputSummary: some View {
        if let p = model.parsed {
            HStack(spacing: 10) {
                Label("\(p.vehicles.count)대 인식", systemImage: "checkmark.circle.fill").foregroundStyle(Theme.ok)
                if p.duplicates > 0 { Text("중복 \(p.duplicates)").foregroundStyle(Theme.warn) }
                if !p.invalid.isEmpty {
                    Text("형식 오류 \(p.invalid.count)줄").foregroundStyle(Theme.bad)
                        .help(p.invalid.prefix(20).joined(separator: "\n"))
                }
            }
            .font(Theme.small)
        }
    }

    private var fileList: some View {
        VStack(alignment: .leading, spacing: 0) {
            if model.files.isEmpty {
                HStack(spacing: 8) {
                    Image(systemName: "tray.and.arrow.down").foregroundStyle(.secondary)
                    Text("여기에 .xlsx 파일을 끌어다 놓으세요").font(Theme.small).foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, minHeight: 56)
            } else {
                ForEach(Array(model.files.enumerated()), id: \.element.id) { i, f in
                    if i > 0 { Divider() }
                    HStack(spacing: 8) {
                        IconTile(symbol: f.encrypted ? "lock.fill" : "tablecells", color: [Theme.accent, Theme.info, Theme.purple, Theme.warn][i % 4], size: 24)
                        VStack(alignment: .leading, spacing: 1) {
                            Text(f.name).lineLimit(1).truncationMode(.middle)
                            Text("\(f.vendors) · \(UI.won(Double(f.records)))건\(f.ignored > 0 ? " · 제외 \(f.ignored)줄" : "")")
                                .font(Theme.small).foregroundStyle(.secondary)
                        }
                        Spacer()
                        Button { model.removeFile(f) } label: { Image(systemName: "xmark.circle.fill").foregroundStyle(.tertiary) }
                            .buttonStyle(.plain).help("첨부 빼기")
                    }
                    .padding(.vertical, 6)
                }
            }
        }
        .padding(.horizontal, 2)
        .background(RoundedRectangle(cornerRadius: 6).fill(dropTarget ? Theme.accent.opacity(0.12) : .clear))
        .onDrop(of: [.fileURL], isTargeted: $dropTarget) { providers in
            Task {
                var urls: [URL] = []
                for p in providers {
                    if let url = try? await p.loadItem(forTypeIdentifier: UTType.fileURL.identifier) as? URL { urls.append(url) }
                    else if let data = try? await p.loadItem(forTypeIdentifier: UTType.fileURL.identifier) as? Data,
                            let url = URL(dataRepresentation: data, relativeTo: nil) { urls.append(url) }
                }
                await model.addFiles(urls)
            }
            return true
        }
    }
}

/// 결과 표
struct DispatchTable: View {
    @ObservedObject var model: DispatchModel

    var body: some View {
        Table(model.visible, selection: $model.selection, sortOrder: $model.sortOrder) {
            TableColumn("완료") { (r: DRow) in
                Toggle("", isOn: Binding(get: { model.completed.contains(r.plate) }, set: { _ in model.toggleDone(r.plate) }))
                    .labelsHidden().toggleStyle(.checkbox).help("완료 표시 (앱을 닫으면 지워져요)")
            }
            .width(40)
            TableColumn("탁송일정", value: \.sortSchedule) { r in choiceCell(r, field: "schedule") }
                .width(min: 112, ideal: 126)
            TableColumn("차량번호", value: \.plate) { r in
                Text(r.plate).fontWeight(.semibold)
                    .foregroundStyle(model.completed.contains(r.plate) ? Theme.ok : model.copied.contains(r.plate) ? Theme.info : Theme.indigo)
                    .onTapGesture(count: 2) { model.copyCell("차량번호", r.plate) }
                    .help(r.sources.isEmpty ? "두 번 눌러 복사" : "원본: \(r.sources)\n두 번 눌러 복사")
            }
            .width(min: 84, ideal: 96)
            TableColumn("주유·충전비용", value: \.sortAmount) { r in
                moneyCell(r.flag("slackPending") ? "확인 중" : r.amount.map(UI.won) ?? "—", copy: r.amount.map { String(Int($0)) } ?? "", label: "주유비용/충전비용")
            }
            .width(min: 80, ideal: 96)
            TableColumn("탁송비용", value: \.sortFee) { r in choiceCell(r, field: "fee") }
                .width(min: 76, ideal: 88)
            TableColumn("부가세") { r in moneyCell(r.vat.map(UI.won) ?? "—", copy: r.vat.map { String(Int($0)) } ?? "", label: "부가세") }
                .width(min: 56, ideal: 66)
            TableColumn("조회 상태", value: \.label) { r in
                StatusDot(text: r.label, color: Theme.statusColor(r.status, progress: r.progress)).help(r.str("meaning"))
            }
            .width(min: 96, ideal: 118)
            TableColumn("업체", value: \.vendor) { r in Text(r.vendor.isEmpty ? "—" : r.vendor) }
                .width(min: 54, ideal: 64)
            TableColumn("탁송 구분") { r in
                Text(r.carrierType).foregroundStyle(r.carrierType == "확인 필요" ? Theme.warn : .primary)
            }
            .width(min: 60, ideal: 70)
            TableColumn("메모·출발지") { r in
                (Text(r.notes).foregroundStyle(Theme.warn) + Text(r.notes.isEmpty || r.origin.isEmpty ? "" : " · ") + Text(r.origin).foregroundStyle(.secondary))
                    .lineLimit(1)
                .help([r.notes, r.origin, r.inputDate.isEmpty ? "" : "입력 일시 \(r.inputDate)"].filter { !$0.isEmpty }.joined(separator: "\n"))
            }
            .width(min: 110, ideal: 180)
        }
        .tableStyle(.inset(alternatesRowBackgrounds: true))
        .contextMenu(forSelectionType: Int.self) { ids in
            Button("차량번호 복사") { model.copyPlates() }
            Button("완료 표시 바꾸기") {
                for r in model.results where ids.contains(r.id) { model.toggleDone(r.plate) }
            }
        }
        .onCopyCommand {
            let plates = model.results.filter { model.selection.contains($0.id) }.map(\.plate)
            model.copied.formUnion(plates)
            return [NSItemProvider(object: plates.joined(separator: "\n") as NSString)]
        }
    }

    private func moneyCell(_ text: String, copy: String, label: String) -> some View {
        Text(text).monospacedDigit().frame(maxWidth: .infinity, alignment: .trailing)
            .contentShape(Rectangle())
            .onTapGesture(count: 2) { model.copyCell(label, copy) }
            .help(copy.isEmpty ? "" : "두 번 눌러 복사 (쉼표 없이)")
    }

    /// 중첩된 값이 있으면 메뉴로 골라요.
    @ViewBuilder private func choiceCell(_ r: DRow, field: String) -> some View {
        let isFee = field == "fee"
        let value = isFee ? (r.fee.map(UI.won) ?? "") : r.schedule
        let copyValue = isFee ? (r.fee.map { String(Int($0)) } ?? "") : r.schedule
        let conflict = r.flag(isFee ? "feeConflict" : "scheduleConflict")
        if isFee && r.distinctValues("schedule") > 1 {
            // 일정이 중첩이면 비용은 고른 일정의 기록을 따라가요.
            if r.flag("scheduleConflict") && r.fee == nil {
                Text("일정 선택 후").foregroundStyle(.secondary).font(Theme.small).frame(maxWidth: .infinity, alignment: .trailing)
            } else { moneyCell(value.isEmpty ? "—" : value, copy: copyValue, label: "탁송비용") }
        } else if r.distinctValues(field) > 1 {
            Menu {
                ForEach(r.candidates(field)) { c in
                    Button("\(c.value) · \(c.vendor) · \(c.state)") { model.choose(r, field: field, id: c.id) }
                        .help(c.source)
                }
            } label: {
                Text(conflict ? "선택 필요" : value).foregroundStyle(conflict ? Theme.warn : .primary)
                    .fontWeight(conflict ? .semibold : .regular)
            }
            .menuStyle(.borderlessButton)
            .help(conflict ? "기록마다 값이 달라요. 맞는 값을 골라 주세요." : "다른 기록의 값으로 바꿀 수 있어요")
        } else if isFee {
            moneyCell(value.isEmpty ? "—" : value, copy: copyValue, label: "탁송비용")
        } else {
            Text(value.isEmpty ? "—" : value).monospacedDigit()
                .contentShape(Rectangle())
                .onTapGesture(count: 2) { model.copyCell("탁송일정", copyValue) }
        }
    }
}

struct SlackRepliesSheet: View {
    @ObservedObject var model: DispatchModel
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("슬랙봇 회신 반영").font(Theme.font(15, .semibold))
            Text("슬랙봇 답변을 ‘차량번호 - 값’ 형식으로 줄마다 붙여넣어요. X는 0원, ‘확인 중’은 확인 중으로 기록해요.")
                .font(Theme.small).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            TextEditor(text: $model.slackText)
                .font(Theme.mono).frame(minHeight: 200)
                .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(Color.primary.opacity(0.15)))
            if !model.slackReport.isEmpty {
                Text(model.slackReport).font(Theme.small).foregroundStyle(Theme.warn).textSelection(.enabled)
            }
            HStack {
                Button("반영한 줄 보기") { model.viewSlackReplies() }.disabled(!model.hasSlackReplies)
                Spacer()
                Button("닫기") { model.showSlack = false }.keyboardShortcut(.cancelAction)
                Button("반영하기") { model.applySlack() }.keyboardShortcut(.defaultAction).tint(Theme.accent)
            }
        }
        .padding(20)
        .frame(width: 520)
        .font(Theme.body)
    }
}
