import 'package:desktop_drop/desktop_drop.dart';
import 'package:file_selector/file_selector.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart';

import '../core/util.dart';
import '../models/dispatch_model.dart';
import '../ui/theme.dart';

class DispatchPage extends StatefulWidget {
  const DispatchPage({super.key, required this.model});
  final DispatchModel model;
  @override
  State<DispatchPage> createState() => _DispatchPageState();
}

class _DispatchPageState extends State<DispatchPage> {
  DispatchModel get m => widget.model;
  bool showPanel = true;
  bool dragging = false;

  static const cols = [
    ('', 40.0, ''), ('탁송일정', 150.0, 'schedule'), ('차량번호', 112.0, 'plate'), ('주유·충전(원)', 112.0, 'amount'),
    ('탁송비용(원)', 108.0, 'fee'), ('부가세(원)', 88.0, 'vat'), ('조회 상태', 136.0, 'label'), ('업체', 84.0, 'vendor'), ('메모', 220.0, ''), ('완료', 52.0, ''),
  ];
  static final tableWidth = cols.fold(0.0, (a, c) => a + c.$2) + 16;

  Future<void> pickFiles() async {
    final files = await openFiles(acceptedTypeGroups: [const XTypeGroup(label: '업체 Excel', extensions: ['xlsx'])]);
    await m.addFiles([for (final f in files) f.path]);
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: m,
      builder: (context, _) => CallbackShortcuts(
        bindings: {
          const SingleActivator(LogicalKeyboardKey.enter, control: true): () {
            setState(() => showPanel = true);
            m.lookup();
          },
          const SingleActivator(LogicalKeyboardKey.keyO, control: true): pickFiles,
        },
        child: ScaffoldPage(
          padding: EdgeInsets.zero,
          header: _header(context),
          content: Row(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
            Expanded(child: _table(context)),
            if (showPanel) _panel(context),
          ]),
        ),
      ),
    );
  }

  // MARK: 머리글·도구 막대

  Widget _header(BuildContext context) {
    final title = DispatchModel.filterTitles.firstWhere((f) => f.$1 == m.filter).$2;
    return Padding(
      padding: const EdgeInsets.fromLTRB(24, 16, 16, 0),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          const IconTile(FluentIcons.car, Tone.green, size: 30),
          const SizedBox(width: 12),
          Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(m.filter == 'all' ? '탁송체크' : '탁송체크 · $title', style: const TextStyle(fontSize: 22, fontWeight: FontWeight.w600)),
            Text(m.stale ? '입력이 바뀌었어요. 다시 조회해 주세요.' : m.status,
                style: TextStyle(fontSize: 12.5, color: m.stale ? Tone.warn(context) : Tone.muted(context))),
          ]),
          const Spacer(),
          if (m.busy) const Padding(padding: EdgeInsets.only(right: 12), child: SizedBox(width: 18, height: 18, child: ProgressRing(strokeWidth: 2.5))),
        ]),
        const SizedBox(height: 10),
        CommandBar(overflowBehavior: CommandBarOverflowBehavior.dynamicOverflow, primaryItems: [
          CommandBarButton(icon: const Icon(FluentIcons.copy, color: Tone.blue), label: const Text('차량번호 복사'), onPressed: m.results.isEmpty ? null : m.copyPlates),
          CommandBarBuilderItem(
            builder: (context, mode, w) => w,
            wrappedItem: CommandBarButton(
              icon: const Icon(FluentIcons.camera, color: Tone.purple),
              label: const Text('결과 캡처 복사'),
              onPressed: m.results.isEmpty ? null : () => m.capture(copy: true),
            ),
          ),
          CommandBarButton(icon: const Icon(FluentIcons.file_image, color: Tone.purple), label: const Text('PNG 저장'), onPressed: m.results.isEmpty ? null : () => m.capture(copy: false)),
          CommandBarButton(icon: const Icon(FluentIcons.text_document, color: Tone.teal), label: const Text('TXT'), onPressed: m.results.isEmpty ? null : () => m.export('txt')),
          CommandBarButton(icon: const Icon(FluentIcons.excel_document, color: Tone.green), label: const Text('엑셀'), onPressed: m.results.isEmpty ? null : () => m.export('xlsx')),
          CommandBarButton(icon: const Icon(FluentIcons.pdf, color: Tone.red), label: const Text('PDF'), onPressed: m.results.isEmpty ? null : () => m.export('pdf')),
          const CommandBarSeparator(),
          if (m.hasSlackTargets)
            CommandBarButton(icon: const Icon(FluentIcons.chat, color: Tone.indigo), label: const Text('슬랙봇 회신'), onPressed: () => _slackDialog(context)),
          CommandBarButton(icon: const Icon(FluentIcons.refresh, color: Tone.orange), label: const Text('처음부터'), onPressed: m.reset),
          CommandBarButton(
            icon: Icon(showPanel ? FluentIcons.close_pane_mirrored : FluentIcons.open_pane_mirrored, color: Tone.grey),
            label: Text(showPanel ? '입력 패널 닫기' : '입력 패널 열기'),
            onPressed: () => setState(() => showPanel = !showPanel),
          ),
        ]),
        const SizedBox(height: 6),
        _scopeBar(context),
        const SizedBox(height: 8),
      ]),
    );
  }

  Widget _scopeBar(BuildContext context) {
    ComboBox<String> combo(String value, List<(String, String)> items, ValueChanged<String> on) => ComboBox<String>(
          value: value,
          items: [for (final (k, t) in items) ComboBoxItem(value: k, child: Text(t))],
          onChanged: (v) => v == null ? null : m.update(() {
            on(v);
            m.page = 0;
          }),
        );
    return Wrap(spacing: 8, runSpacing: 6, crossAxisAlignment: WrapCrossAlignment.center, children: [
      SizedBox(
        width: 180,
        child: TextBox(
          controller: m.search,
          placeholder: '차량번호 검색',
          prefix: const Padding(padding: EdgeInsets.only(left: 8), child: Icon(FluentIcons.search, size: 12)),
          onChanged: (_) => m.update(() => m.page = 0),
        ),
      ),
      combo(m.vendorFilter, [('all', '모든 업체'), for (final v in m.vendorOptions) (v, vendorName(v))], (v) => m.vendorFilter = v),
      combo(m.stateFilter, [('all', '모든 상태'), for (final s in m.stateOptions) (s, s)], (v) => m.stateFilter = v),
      Row(mainAxisSize: MainAxisSize.min, children: [
        Text('저장 범위 ', style: TextStyle(color: Tone.muted(context), fontSize: 12.5)),
        combo(m.scope, DispatchModel.scopes, (v) => m.scope = v),
      ]),
      if (m.selection.isNotEmpty)
        Row(mainAxisSize: MainAxisSize.min, children: [
          Text('선택 ${m.selection.length}줄 ', style: TextStyle(color: Tone.info(context), fontWeight: FontWeight.w600)),
          HyperlinkButton(onPressed: () => m.update(() => m.selection = {}), child: const Text('선택 해제')),
        ]),
    ]);
  }

  // MARK: 표

  Widget _table(BuildContext context) {
    final res = FluentTheme.of(context).resources;
    if (m.results.isEmpty) {
      return Center(
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          const IconTile(FluentIcons.car, Tone.green, size: 56),
          const SizedBox(height: 14),
          const Text('조회 결과가 여기에 나와요', style: TextStyle(fontSize: 17, fontWeight: FontWeight.w600)),
          const SizedBox(height: 6),
          Text('오른쪽 패널에 차량목록을 붙여넣고 업체 엑셀을 넣은 뒤\n차량 조회하기(Ctrl+Enter)를 눌러 주세요.',
              textAlign: TextAlign.center, style: TextStyle(color: Tone.muted(context))),
        ]),
      );
    }
    final rows = m.visible;
    final pages = rows.isEmpty ? 1 : (rows.length / DispatchModel.pageSize).ceil();
    if (m.page >= pages) m.page = pages - 1;
    final shown = rows.skip(m.page * DispatchModel.pageSize).take(DispatchModel.pageSize).toList();
    return LayoutBuilder(builder: (context, box) {
      final width = box.maxWidth > tableWidth ? box.maxWidth : tableWidth;
      return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Expanded(
          child: Scrollbar(
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: SizedBox(
                width: width,
                child: Column(children: [
                  _headRow(context, shown),
                  Container(height: 1, color: res.dividerStrokeColorDefault),
                  Expanded(
                    child: shown.isEmpty
                        ? Center(child: Text('이 보기에 해당하는 차량이 없어요.', style: TextStyle(color: Tone.muted(context))))
                        : ListView.builder(itemCount: shown.length, itemExtent: 42, itemBuilder: (context, i) => _row(context, shown[i], i)),
                  ),
                ]),
              ),
            ),
          ),
        ),
        _footer(context, rows, pages),
      ]);
    });
  }

  Widget _headRow(BuildContext context, List<DRow> shown) {
    final allPicked = shown.isNotEmpty && shown.every((r) => m.selection.contains(r.id));
    return SizedBox(
      height: 38,
      child: Row(children: [
        const SizedBox(width: 8),
        for (final (t, w, key) in cols)
          SizedBox(
            width: w,
            child: t.isEmpty
                ? Checkbox(
                    checked: allPicked,
                    onChanged: (v) => m.update(() {
                      if (v == true) {
                        m.selection.addAll(shown.map((r) => r.id));
                      } else {
                        m.selection.removeAll(shown.map((r) => r.id));
                      }
                    }),
                  )
                : key.isEmpty
                    ? Text(t, style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 12.5))
                    : GestureDetector(
                        onTap: () => m.sortBy(key),
                        child: MouseRegion(
                          cursor: SystemMouseCursors.click,
                          child: Row(children: [
                            Text(t, style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 12.5)),
                            if (m.sortKey == key) Icon(m.sortAsc ? FluentIcons.chevron_up_small : FluentIcons.chevron_down_small, size: 10),
                          ]),
                        ),
                      ),
          ),
      ]),
    );
  }

  Widget _copyable(BuildContext context, String text, {TextStyle? style, bool right = false}) {
    final child = Text(text, maxLines: 1, overflow: TextOverflow.ellipsis, textAlign: right ? TextAlign.right : TextAlign.left, style: style);
    if (text.isEmpty || text == '—') return child;
    return Tooltip(
      message: '눌러서 복사',
      child: GestureDetector(onTap: () => m.copyCell(text), child: MouseRegion(cursor: SystemMouseCursors.click, child: child)),
    );
  }

  Widget _row(BuildContext context, DRow r, int i) {
    final res = FluentTheme.of(context).resources;
    final done = m.completed.contains(r.plate);
    final picked = m.selection.contains(r.id);
    final (fg, bg) = Tone.status(context, r.raw);
    final money = TextStyle(fontFeatures: const [FontFeature.tabularFigures()], color: done ? Tone.muted(context) : null);
    Widget cell(double w, Widget child, {bool right = false}) => SizedBox(
          width: w,
          child: Padding(padding: EdgeInsets.only(right: right ? 14 : 8), child: Align(alignment: right ? Alignment.centerRight : Alignment.centerLeft, child: child)),
        );
    Widget scheduleCell() {
      if (r.flag('scheduleConflict') && r.schedule.isEmpty) {
        return Button(onPressed: () => _choose(context, r, 'schedule'), child: Text('일정 선택 (${r.candidates('schedule').length})', style: TextStyle(color: Tone.warn(context))));
      }
      return _copyable(context, r.schedule.isEmpty ? '—' : r.schedule, style: money);
    }

    Widget feeCell() {
      if (r.flag('feeConflict') && r.fee == null) {
        if (r.flag('scheduleConflict')) return Text('일정 선택 후 표시', style: TextStyle(fontSize: 12, color: Tone.muted(context)));
        return Button(onPressed: () => _choose(context, r, 'fee'), child: Text('비용 선택', style: TextStyle(color: Tone.warn(context))));
      }
      return _copyable(context, numText(r.fee), style: money, right: true);
    }

    return GestureDetector(
      onTap: () => m.toggleSelect(r.id),
      child: Container(
        decoration: BoxDecoration(
          color: picked ? FluentTheme.of(context).accentColor.withValues(alpha: 0.12) : (i.isOdd ? res.subtleFillColorSecondary : null),
          border: Border(bottom: BorderSide(color: res.dividerStrokeColorDefault.withValues(alpha: 0.5))),
        ),
        child: Row(children: [
          const SizedBox(width: 8),
          cell(40, Checkbox(checked: picked, onChanged: (_) => m.toggleSelect(r.id))),
          cell(150, scheduleCell()),
          cell(112, Row(children: [
            Flexible(child: _copyable(context, r.plate, style: TextStyle(fontWeight: FontWeight.w700, color: done ? Tone.muted(context) : Tone.indigo))),
            if (m.copied.contains(r.plate))
              Padding(padding: const EdgeInsets.only(left: 4), child: Tooltip(message: '오늘 복사함', child: Icon(FluentIcons.copy, size: 11, color: Tone.muted(context)))),
          ])),
          cell(112, r.flag('slackPending') ? Text('확인 중', style: TextStyle(color: Tone.info(context))) : _copyable(context, numText(r.amount), style: money, right: true), right: true),
          cell(108, feeCell(), right: true),
          cell(88, _copyable(context, numText(r.vat), style: money, right: true), right: true),
          cell(136, Pill(r.label.isEmpty ? '—' : r.label, fg, bg)),
          cell(84, Text(r.vendor.isEmpty ? '—' : vendorName(r.vendor), style: TextStyle(color: Tone.muted(context), fontSize: 12.5))),
          cell(220, Tooltip(message: r.notes, child: Text(r.notes, maxLines: 1, overflow: TextOverflow.ellipsis, style: TextStyle(fontSize: 12.5, color: r.unresolved ? Tone.warn(context) : Tone.muted(context))))),
          cell(52, Checkbox(checked: done, onChanged: (_) => m.toggleDone(r.plate))),
        ]),
      ),
    );
  }

  Widget _footer(BuildContext context, List<DRow> rows, int pages) {
    final res = FluentTheme.of(context).resources;
    final unique = rules(rows);
    double sum(String k) => unique.fold(0.0, (a, r) => a + ((r[k] as num?)?.toDouble() ?? 0));
    final from = rows.isEmpty ? 0 : m.page * DispatchModel.pageSize + 1;
    final to = (m.page + 1) * DispatchModel.pageSize > rows.length ? rows.length : (m.page + 1) * DispatchModel.pageSize;
    return Container(
      height: 40,
      padding: const EdgeInsets.symmetric(horizontal: 16),
      decoration: BoxDecoration(border: Border(top: BorderSide(color: res.dividerStrokeColorDefault))),
      child: Row(children: [
        Text('${unique.length}대', style: const TextStyle(fontWeight: FontWeight.w600)),
        const SizedBox(width: 16),
        Expanded(
          child: Text('주유·충전 ${won(sum('amount'))}원 · 탁송비용 ${won(sum('dispatchFee'))}원 · 부가세 ${won(sum('vat'))}원',
              maxLines: 1, overflow: TextOverflow.ellipsis, style: TextStyle(color: Tone.ok(context), fontWeight: FontWeight.w600, fontSize: 12.5)),
        ),
        if (pages > 1) ...[
          Text('$from–$to / ${rows.length}', style: TextStyle(color: Tone.muted(context), fontSize: 12.5)),
          IconButton(icon: const Icon(FluentIcons.chevron_left, size: 12), onPressed: m.page == 0 ? null : () => m.update(() => m.page--)),
          IconButton(icon: const Icon(FluentIcons.chevron_right, size: 12), onPressed: m.page >= pages - 1 ? null : () => m.update(() => m.page++)),
        ],
      ]),
    );
  }

  List<Map<String, dynamic>> rules(List<DRow> rows) => m.rules.unique([for (final r in rows) r.raw]);

  Future<void> _choose(BuildContext context, DRow r, String field) async {
    final list = r.candidates(field);
    final id = await showDialog<String>(
      context: context,
      builder: (context) => ContentDialog(
        title: Text('${r.plate} · ${field == 'schedule' ? '탁송일정' : '탁송비용'} 선택'),
        content: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Text('업체 기록이 여러 개예요. 맞는 기록을 골라 주세요.', style: TextStyle(color: Tone.muted(context))),
          const SizedBox(height: 10),
          for (final c in list)
            Padding(
              padding: const EdgeInsets.only(bottom: 6),
              child: Button(
                onPressed: () => Navigator.pop(context, c.id),
                child: Padding(
                  padding: const EdgeInsets.symmetric(vertical: 4),
                  child: Row(children: [
                    Expanded(child: Text(c.value, style: const TextStyle(fontWeight: FontWeight.w600))),
                    Text([c.vendor, c.state, c.source].where((x) => x.isNotEmpty).join(' · '), style: TextStyle(color: Tone.muted(context), fontSize: 12)),
                  ]),
                ),
              ),
            ),
        ]),
        actions: [Button(onPressed: () => Navigator.pop(context), child: const Text('취소'))],
      ),
    );
    if (id != null) m.choose(r, field, id);
  }

  Future<void> _slackDialog(BuildContext context) async {
    await showDialog<void>(
      context: context,
      builder: (context) => ListenableBuilder(
        listenable: m,
        builder: (context, _) => ContentDialog(
          constraints: const BoxConstraints(maxWidth: 560),
          title: const Text('슬랙봇 회신 반영'),
          content: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
            Text('슬랙봇 문의 차량의 회신을 줄마다 ‘차량번호 - 값’으로 붙여넣어 주세요. X는 0원, ‘확인 중’은 그대로 기록해요.',
                style: TextStyle(color: Tone.muted(context), fontSize: 12.5)),
            const SizedBox(height: 10),
            TextBox(controller: m.slackText, autofocus: true, maxLines: 9, minLines: 9, placeholder: '08구8701 - X\n77다1234 - 12,350원\n12가3456 - 확인 중', style: Tone.mono),
            if (m.slackReport.isNotEmpty) ...[const SizedBox(height: 10), SelectableText(m.slackReport, style: const TextStyle(fontSize: 12.5))],
          ]),
          actions: [
            FilledButton(onPressed: () {
              if (m.applySlack()) Navigator.pop(context);
            }, child: const Text('표에 반영하기')),
            Button(onPressed: () => Navigator.pop(context), child: const Text('닫기')),
          ],
        ),
      ),
    );
  }

  // MARK: 오른쪽 입력 패널

  Widget _panel(BuildContext context) {
    final p = m.parsed;
    final res = FluentTheme.of(context).resources;
    return DropTarget(
      onDragEntered: (_) => setState(() => dragging = true),
      onDragExited: (_) => setState(() => dragging = false),
      onDragDone: (d) {
        setState(() => dragging = false);
        m.addFiles([for (final f in d.files) f.path]);
      },
      child: SidePanel(children: [
        SectionTitle('차량목록', FluentIcons.bulleted_list, Tone.blue,
            trailing: p == null
                ? null
                : Text('${p.vehicles.length}대${p.invalid.isNotEmpty ? ' · 확인 ${p.invalid.length}줄' : ''}',
                    style: TextStyle(fontSize: 12.5, color: p.invalid.isNotEmpty ? Tone.warn(context) : Tone.ok(context), fontWeight: FontWeight.w600))),
        TextBox(
          controller: m.input,
          maxLines: 12,
          minLines: 12,
          style: Tone.mono,
          placeholder: '차량번호를 줄마다 붙여넣어요\n12가3456\n34나5678 2026-11-27 09:30',
        ),
        const SizedBox(height: 4),
        Text('날짜·시간을 같이 넣으면 그 값을 탁송일정으로 써요. 빈 줄은 알아서 지워요.', style: TextStyle(fontSize: 11.5, color: Tone.muted(context))),
        const SizedBox(height: 18),
        SectionTitle('업체 파일', FluentIcons.excel_document, Tone.green, trailing: Text('${m.files.length}개', style: TextStyle(color: Tone.muted(context), fontSize: 12.5))),
        GestureDetector(
          onTap: pickFiles,
          child: MouseRegion(
            cursor: SystemMouseCursors.click,
            child: Container(
              padding: const EdgeInsets.symmetric(vertical: 16, horizontal: 12),
              decoration: BoxDecoration(
                color: dragging ? Tone.green.withValues(alpha: 0.14) : res.subtleFillColorSecondary,
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: dragging ? Tone.green : res.controlStrokeColorDefault, width: dragging ? 2 : 1),
              ),
              child: Column(children: [
                Icon(FluentIcons.upload, color: Tone.green, size: 20),
                const SizedBox(height: 6),
                const Text('엑셀 파일을 끌어다 놓거나 눌러서 선택', style: TextStyle(fontWeight: FontWeight.w600)),
                Text('한빛탁송·오픈마일·렌톡 .xlsx · 암호 파일도 돼요 (Ctrl+O)', style: TextStyle(fontSize: 11.5, color: Tone.muted(context))),
              ]),
            ),
          ),
        ),
        const SizedBox(height: 8),
        for (final f in m.files)
          Padding(
            padding: const EdgeInsets.only(bottom: 4),
            child: Row(children: [
              IconTile(f.encrypted ? FluentIcons.lock : FluentIcons.excel_document, f.encrypted ? Tone.amber : Tone.green, size: 26),
              const SizedBox(width: 8),
              Expanded(
                child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  Text(f.name, maxLines: 1, overflow: TextOverflow.ellipsis, style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 12.5)),
                  Text('${f.vendors.isEmpty ? '업체 확인 안 됨' : f.vendors} · 기록 ${f.records}건${f.ignored > 0 ? ' · 제외 ${f.ignored}건' : ''}',
                      style: TextStyle(fontSize: 11.5, color: Tone.muted(context))),
                ]),
              ),
              IconButton(icon: const Icon(FluentIcons.cancel, size: 11), onPressed: () => m.removeFile(f)),
            ]),
          ),
        const SizedBox(height: 16),
        FilledButton(
          onPressed: m.canLookup ? m.lookup : null,
          child: const Padding(
            padding: EdgeInsets.symmetric(vertical: 6),
            child: Row(mainAxisAlignment: MainAxisAlignment.center, children: [
              Icon(FluentIcons.search, size: 14),
              SizedBox(width: 8),
              Text('차량 조회하기', style: TextStyle(fontWeight: FontWeight.w600)),
              SizedBox(width: 8),
              Text('Ctrl+Enter', style: TextStyle(fontSize: 11.5)),
            ]),
          ),
        ),
      ]),
    );
  }
}
