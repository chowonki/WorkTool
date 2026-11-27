import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart';

import '../core/util.dart';
import '../models/transfer_model.dart';
import '../ui/theme.dart';
import '../ui/ui.dart';

class TransferPage extends StatefulWidget {
  const TransferPage({super.key, required this.model});
  final TransferModel model;
  @override
  State<TransferPage> createState() => _TransferPageState();
}

class _TransferPageState extends State<TransferPage> {
  TransferModel get m => widget.model;
  late final List<String> banks = [for (final g in m.rules.bankGroups) ...g.$2];

  static const stepTitles = {1: '이체정보 입력', 2: '확인·수정', 3: '다운로드'};

  @override
  Widget build(BuildContext context) => ListenableBuilder(
        listenable: m,
        builder: (context, _) => CallbackShortcuts(
          bindings: {
            const SingleActivator(LogicalKeyboardKey.enter, control: true): () {
              if (m.step == 1) _extract();
              if (m.step == 3) m.download();
            },
          },
          child: ScaffoldPage(
            padding: EdgeInsets.zero,
            header: _header(context),
            content: switch (m.step) { 1 => _step1(context), 2 => _step2(context), _ => _step3(context) },
          ),
        ),
      );

  // MARK: 머리글

  Widget _header(BuildContext context) {
    final c = m.counts;
    final items = <CommandBarItem>[
      if (m.step == 1) ...[
        CommandBarButton(icon: const Icon(FluentIcons.add, color: Tone.blue), label: const Text('입력칸 추가'), onPressed: m.addThread),
        if (m.rows.isNotEmpty) CommandBarButton(icon: const Icon(FluentIcons.forward, color: Tone.orange), label: Text('확인·수정으로 (${c.all}건)'), onPressed: () => m.setStep(2)),
      ],
      if (m.step == 2) ...[
        CommandBarButton(icon: const Icon(FluentIcons.back, color: Tone.grey), label: const Text('1단계로'), onPressed: () => m.setStep(1)),
        CommandBarButton(icon: const Icon(FluentIcons.add, color: Tone.blue), label: const Text('행 추가'), onPressed: m.addRow),
        CommandBarButton(icon: Icon(FluentIcons.delete, color: Tone.red), label: Text(m.selection.isEmpty ? '삭제' : '삭제 (${m.selection.length})'), onPressed: m.selection.isEmpty ? null : m.deleteSelected),
      ],
      if (m.step == 3) CommandBarButton(icon: const Icon(FluentIcons.edit, color: Tone.orange), label: const Text('2단계에서 고치기'), onPressed: () => m.setStep(2)),
      const CommandBarSeparator(),
      CommandBarButton(icon: const Icon(FluentIcons.refresh, color: Tone.orange), label: const Text('처음부터'), onPressed: m.resetAll),
    ];
    return Padding(
      padding: const EdgeInsets.fromLTRB(24, 16, 16, 8),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          const IconTile(FluentIcons.bank, Tone.orange, size: 30),
          const SizedBox(width: 12),
          Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text('이체파일 · ${m.step}단계 ${stepTitles[m.step]}', style: const TextStyle(fontSize: 22, fontWeight: FontWeight.w600)),
            Text(
              m.rows.isEmpty ? 'Slack 스레드를 붙여넣고 이체정보를 추출해요.' : '총 ${c.all}건 · 정상 ${c.ok} · 확인 ${c.warn} · 오류 ${c.bad}${m.extractedAt.isEmpty ? '' : ' · ${m.extractedAt} 추출'}',
              style: TextStyle(fontSize: 12.5, color: Tone.muted(context)),
            ),
          ]),
          const Spacer(),
          for (final n in [1, 2, 3])
            Padding(
              padding: const EdgeInsets.only(left: 6),
              child: ToggleButton(
                checked: m.step == n,
                onChanged: (_) => m.setStep(n),
                child: Text('$n ${stepTitles[n]}'),
              ),
            ),
        ]),
        const SizedBox(height: 10),
        Row(children: [
          Expanded(child: CommandBar(overflowBehavior: CommandBarOverflowBehavior.dynamicOverflow, primaryItems: items)),
          if (m.step == 1) FilledButton(onPressed: _extract, child: const _BtnText(FluentIcons.play, '이체정보 추출하기', 'Ctrl+Enter')),
          if (m.step == 2) FilledButton(onPressed: m.goStep3, child: const _BtnText(FluentIcons.forward, '다운로드 단계로', '')),
          if (m.step == 3) FilledButton(onPressed: m.canDownload ? m.download : null, child: const _BtnText(FluentIcons.download, '이체파일 다운로드', 'Ctrl+Enter')),
        ]),
      ]),
    );
  }

  // MARK: 1단계

  Future<void> _extract() => m.extract((noPlate, warnings) => _askPlates(noPlate, warnings));

  Widget _step1(BuildContext context) => Row(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Expanded(
          child: ListView(padding: const EdgeInsets.fromLTRB(24, 4, 24, 24), children: [
            for (var i = 0; i < m.threads.length; i++) ...[
              Row(children: [
                IconTile(FluentIcons.chat, Tone.blue, size: 20),
                const SizedBox(width: 8),
                Text('이체 요청 ${i + 1}', style: const TextStyle(fontWeight: FontWeight.w600)),
                if (m.threads[i].imported.isNotEmpty && m.threads[i].imported == m.threads[i].text.text.trim())
                  Padding(padding: const EdgeInsets.only(left: 8), child: Pill('추출함', Tone.ok(context), Tone.ok(context).withValues(alpha: 0.12))),
                const Spacer(),
                IconButton(icon: const Icon(FluentIcons.delete, size: 13), onPressed: () => m.removeThread(m.threads[i])),
              ]),
              const SizedBox(height: 6),
              TextBox(
                controller: m.threads[i].text,
                maxLines: 10,
                minLines: 6,
                placeholder: 'Slack 스레드 내용을 그대로 붙여넣어요.\n예) 현대 쏘나타 123가4567 / 국민은행 123-456-789012 / 35,000원',
                onChanged: (_) => setState(() {}),
              ),
              const SizedBox(height: 16),
            ],
            Align(alignment: Alignment.centerLeft, child: Button(onPressed: m.addThread, child: const Text('+ 입력칸 추가'))),
          ]),
        ),
        SidePanel(children: [
          SectionTitle('CMS코드 (선택)', FluentIcons.numbered_list, Tone.teal),
          TextBox(controller: m.cmsAll, placeholder: '모든 건에 넣을 CMS코드'),
          const SizedBox(height: 18),
          SectionTitle('이렇게 해요', FluentIcons.lightbulb, Tone.amber),
          for (final t in [
            '스레드가 여러 개면 입력칸을 추가해 나눠 넣어요.',
            '이미 추출한 스레드에 새 내용을 더하면, 새로 생긴 건만 더해요.',
            '1억을 넘는 금액은 1억씩 나눠 여러 줄로 만들어요.',
            '최대 100건까지 한 파일로 만들 수 있어요.',
          ])
            Padding(padding: const EdgeInsets.only(bottom: 6, left: 28), child: Text(t, style: TextStyle(color: Tone.muted(context), fontSize: 12.5))),
          if (m.extractInfo.isNotEmpty) ...[
            const SizedBox(height: 12),
            Text(m.extractInfo, style: TextStyle(color: Tone.ok(context), fontWeight: FontWeight.w600)),
          ],
        ]),
      ]);

  Future<void> _askPlates(List<TRow> noPlate, List<String> warnings) async {
    final usePlate = {for (final r in noPlate) r.id: true};
    final ctrls = {for (final r in noPlate) r.id: TextEditingController()};
    String error = '';
    await showDialog<void>(
      context: context,
      builder: (context) => StatefulBuilder(
        builder: (context, setLocal) => ContentDialog(
          constraints: const BoxConstraints(maxWidth: 640),
          title: Text('차량번호가 없는 ${noPlate.length}건이 있어요'),
          content: SizedBox(
            height: 380,
            child: ListView(children: [
              Text('차량번호를 넣거나, 차량번호 대신 입금통장 메모를 넣어 주세요. 비워 두면 2단계에서 고칠 수 있어요.', style: TextStyle(color: Tone.muted(context), fontSize: 12.5)),
              if (warnings.isNotEmpty) Padding(padding: const EdgeInsets.only(top: 6), child: Text(warnings.join('\n'), style: TextStyle(color: Tone.warn(context), fontSize: 12.5))),
              const SizedBox(height: 10),
              for (final r in noPlate)
                Padding(
                  padding: const EdgeInsets.only(bottom: 12),
                  child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                    Text([if (r.label.isNotEmpty) r.label, r.bank, r.account, if (r.amount.isNotEmpty) '${won(double.tryParse(r.amount) ?? 0)}원', if (r.holder.isNotEmpty) r.holder].join(' · '),
                        style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 12.5)),
                    const SizedBox(height: 4),
                    Row(children: [
                      ToggleButton(checked: usePlate[r.id]!, onChanged: (_) => setLocal(() => usePlate[r.id] = true), child: const Text('차량번호')),
                      const SizedBox(width: 4),
                      ToggleButton(checked: !usePlate[r.id]!, onChanged: (_) => setLocal(() => usePlate[r.id] = false), child: const Text('입금통장 메모')),
                      const SizedBox(width: 8),
                      Expanded(child: TextBox(controller: ctrls[r.id], placeholder: usePlate[r.id]! ? '예) 123가4567' : '예) 홍길동 환급')),
                    ]),
                  ]),
                ),
              if (error.isNotEmpty) Text(error, style: TextStyle(color: Tone.bad(context), fontWeight: FontWeight.w600)),
            ]),
          ),
          actions: [
            FilledButton(
              onPressed: () {
                final bad = m.applyPlates([for (final r in noPlate) (id: r.id, usePlate: usePlate[r.id]!, text: ctrls[r.id]!.text)]);
                if (bad != null) {
                  setLocal(() => error = '‘$bad’는 차량번호 형식이 아니에요. 고쳐 주세요.');
                } else {
                  Navigator.pop(context);
                }
              },
              child: const Text('반영하기'),
            ),
            Button(onPressed: () => Navigator.pop(context), child: const Text('나중에 2단계에서')),
          ],
        ),
      ),
    );
  }

  // MARK: 2단계

  static const cols = [('', 40.0), ('#', 44.0), ('은행', 150.0), ('계좌번호', 170.0), ('입금액(원)', 130.0), ('차량번호', 116.0),
    ('출금통장 메모', 150.0), ('입금통장 메모', 150.0), ('CMS', 90.0), ('상태', 120.0)];
  static final tableWidth = cols.fold(0.0, (a, c) => a + c.$2) + 24;

  Widget _step2(BuildContext context) {
    final c = m.counts;
    final rows = m.visibleRows;
    final res = FluentTheme.of(context).resources;
    final focusedRow = m.rows.where((r) => r.id == m.focused).firstOrNull;
    return Row(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      Expanded(
        child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(24, 0, 16, 8),
            child: Wrap(spacing: 6, runSpacing: 6, crossAxisAlignment: WrapCrossAlignment.center, children: [
              for (final (k, t, n, color) in [('all', '전체', c.all, Tone.grey), ('ok', '정상', c.ok, Tone.ok(context)), ('warn', '확인', c.warn, Tone.warn(context)), ('bad', '오류', c.bad, Tone.bad(context))])
                ToggleButton(
                  checked: m.filter == k,
                  onChanged: (_) => setState(() => m.filter = k),
                  child: Row(mainAxisSize: MainAxisSize.min, children: [
                    if (k != 'all') Container(width: 7, height: 7, margin: const EdgeInsets.only(right: 6), decoration: BoxDecoration(color: color, shape: BoxShape.circle)),
                    Text('$t $n'),
                  ]),
                ),
              const SizedBox(width: 8),
              Checkbox(checked: m.autoFix, onChanged: (v) => m.setAutoFix(v ?? false), content: const Text('계좌번호 하이픈·공백 자동 제거')),
              const SizedBox(width: 8),
              Text('총 입금액 ${won(c.sum)}원', style: TextStyle(color: Tone.ok(context), fontWeight: FontWeight.w700)),
            ]),
          ),
          Expanded(
            child: LayoutBuilder(builder: (context, box) {
              final width = box.maxWidth > tableWidth ? box.maxWidth : tableWidth;
              return Scrollbar(
                child: SingleChildScrollView(
                  scrollDirection: Axis.horizontal,
                  child: SizedBox(
                    width: width,
                    child: Column(children: [
                      SizedBox(
                        height: 36,
                        child: Row(children: [
                          const SizedBox(width: 16),
                          for (final (t, w) in cols)
                            SizedBox(
                              width: w,
                              child: t.isEmpty
                                  ? Align(alignment: Alignment.centerLeft, child: Checkbox(
                                      checked: rows.isNotEmpty && rows.every((r) => m.selection.contains(r.id)),
                                      onChanged: (v) => setState(() => v == true ? m.selection.addAll(rows.map((r) => r.id)) : m.selection.removeAll(rows.map((r) => r.id))),
                                    ))
                                  : Text(t, style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 12.5)),
                            ),
                        ]),
                      ),
                      Container(height: 1, color: res.dividerStrokeColorDefault),
                      Expanded(
                        child: rows.isEmpty
                            ? Center(child: Text('이 보기에 해당하는 건이 없어요.', style: TextStyle(color: Tone.muted(context))))
                            : ListView.builder(itemCount: rows.length, itemExtent: 46, itemBuilder: (context, i) => _editRow(context, rows[i])),
                      ),
                    ]),
                  ),
                ),
              );
            }),
          ),
        ]),
      ),
      _inspector(context, focusedRow),
    ]);
  }

  Widget _editRow(BuildContext context, TRow r) {
    final res = FluentTheme.of(context).resources;
    final check = m.checks[r.id] ?? const TCheck();
    final state = check.state;
    final color = state == 'bad' ? Tone.bad(context) : state == 'warn' ? Tone.warn(context) : Tone.ok(context);
    final focused = m.focused == r.id;
    final picked = m.selection.contains(r.id);
    bool err(String prefix) => check.errors.any((e) => e.startsWith(prefix));
    final idx = m.rows.indexOf(r) + 1;
    final split = m.splitLabels[r.id];
    Widget box(String field, double w, {bool bad = false, bool warn = false, bool right = false}) => SizedBox(
          width: w,
          child: Padding(
            padding: const EdgeInsets.only(right: 6),
            child: _CellBox(
              key: ValueKey('${r.id}/$field'),
              model: m,
              row: r,
              field: field,
              revision: m.revision,
              tone: bad ? Tone.bad(context) : warn ? Tone.warn(context) : null,
              right: right,
              onFocus: () => setState(() => m.focused = r.id),
            ),
          ),
        );
    return GestureDetector(
      onTap: () => setState(() => m.focused = r.id),
      child: Container(
        decoration: BoxDecoration(
          color: focused ? FluentTheme.of(context).accentColor.withValues(alpha: 0.10) : state == 'bad' ? Tone.bad(context).withValues(alpha: 0.05) : null,
          border: Border(bottom: BorderSide(color: res.dividerStrokeColorDefault.withValues(alpha: 0.5))),
        ),
        child: Row(children: [
          const SizedBox(width: 16),
          SizedBox(width: 40, child: Align(alignment: Alignment.centerLeft, child: Checkbox(checked: picked, onChanged: (_) => setState(() => picked ? m.selection.remove(r.id) : m.selection.add(r.id))))),
          SizedBox(width: 44, child: Text('$idx', style: TextStyle(color: Tone.muted(context), fontSize: 12.5))),
          SizedBox(
            width: 150,
            child: Padding(
              padding: const EdgeInsets.only(right: 6),
              child: ComboBox<String>(
                isExpanded: true,
                value: r.bank.isEmpty ? null : r.bank,
                placeholder: Text('은행 선택', style: TextStyle(color: Tone.bad(context))),
                items: [
                  if (r.bank.isNotEmpty && !banks.contains(r.bank)) ComboBoxItem(value: r.bank, child: Text('${r.bank} (확인)', style: TextStyle(color: Tone.bad(context)))),
                  for (final b in banks) ComboBoxItem(value: b, child: Text(b)),
                ],
                onChanged: (v) {
                  if (v != null) m.edit(r.id, 'bank', v);
                  setState(() => m.focused = r.id);
                },
              ),
            ),
          ),
          box('account', 170, bad: err('계좌')),
          box('amount', 130, bad: err('금액') || err('1억'), right: true),
          box('plate', 116, warn: check.reviews.any((e) => e.startsWith('차량번호'))),
          box('outMemo', 150),
          box('inMemo', 150),
          box('cms', 90),
          SizedBox(
            width: 120,
            child: Tooltip(
              message: [...check.errors, ...check.reviews].join('\n'),
              child: Row(children: [
                Flexible(child: Pill(state == 'bad' ? check.errors.first : state == 'warn' ? check.reviews.first : '정상', color, color.withValues(alpha: 0.12))),
                if (split != null) Padding(padding: const EdgeInsets.only(left: 4), child: Text(split, style: TextStyle(fontSize: 11, color: Tone.info(context)))),
              ]),
            ),
          ),
        ]),
      ),
    );
  }

  Widget _inspector(BuildContext context, TRow? r) {
    final check = r == null ? null : m.checks[r.id];
    Widget memo(String title, TextEditingController c, String field) => Padding(
          padding: const EdgeInsets.only(bottom: 8),
          child: Row(children: [
            Expanded(child: TextBox(controller: c, placeholder: title)),
            const SizedBox(width: 6),
            Button(onPressed: () => m.applyMemo(field, c.text.trim(), title), child: const Text('적용')),
          ]),
        );
    return SidePanel(children: [
      SectionTitle('원문', FluentIcons.chat, Tone.blue, trailing: r == null ? null : Text('이체 요청 ${r.thread}', style: TextStyle(color: Tone.muted(context), fontSize: 12))),
      if (r == null)
        Text('표에서 줄을 고르면 그 건의 Slack 원문이 여기에 보여요.', style: TextStyle(color: Tone.muted(context), fontSize: 12.5))
      else ...[
        if (r.label.isNotEmpty) Padding(padding: const EdgeInsets.only(bottom: 6), child: Pill(r.label, Tone.info(context), Tone.info(context).withValues(alpha: 0.12))),
        Container(
          padding: const EdgeInsets.all(10),
          decoration: BoxDecoration(color: FluentTheme.of(context).resources.subtleFillColorSecondary, borderRadius: BorderRadius.circular(6)),
          child: SelectableText(r.ctx.isEmpty ? '(원문 없음 · 직접 추가한 줄)' : r.ctx, style: const TextStyle(fontSize: 12.5, height: 1.45)),
        ),
        const SizedBox(height: 14),
        SectionTitle('확인 사항', FluentIcons.warning, Tone.orange),
        if (check == null || (check.errors.isEmpty && check.reviews.isEmpty))
          Text('문제 없어요.', style: TextStyle(color: Tone.ok(context)))
        else ...[
          for (final e in check.errors) Padding(padding: const EdgeInsets.only(bottom: 4), child: Text('• $e', style: TextStyle(color: Tone.bad(context)))),
          for (final e in check.reviews) Padding(padding: const EdgeInsets.only(bottom: 4), child: Text('• $e', style: TextStyle(color: Tone.warn(context)))),
        ],
      ],
      const SizedBox(height: 18),
      SectionTitle('통장 메모·CMS 일괄 입력', FluentIcons.edit_note, Tone.teal),
      memo('출금통장 메모', m.outMemoAll, 'outMemo'),
      memo('입금통장 메모', m.inMemoAll, 'inMemo'),
      memo('CMS코드', m.cmsAll, 'cms'),
      Checkbox(checked: m.onlyEmpty, onChanged: (v) => setState(() => m.onlyEmpty = v ?? true), content: const Text('빈 칸에만 넣기')),
    ]);
  }

  // MARK: 3단계

  Widget _step3(BuildContext context) {
    final c = m.counts;
    final fileRows = m.fileRows;
    final comments = m.comments;
    final res = FluentTheme.of(context).resources;
    return Row(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      Expanded(
        child: ListView(padding: const EdgeInsets.fromLTRB(24, 0, 24, 24), children: [
          SectionTitle('이체파일 미리보기', FluentIcons.table, Tone.green, trailing: Text('${c.all}건 · ${won(c.sum)}원', style: TextStyle(color: Tone.ok(context), fontWeight: FontWeight.w700))),
          Scrollbar(
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: Container(
                decoration: BoxDecoration(border: Border.all(color: res.cardStrokeColorDefault), borderRadius: BorderRadius.circular(6)),
                child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  for (var i = 0; i < fileRows.length; i++)
                    Container(
                      color: i == 0 ? res.subtleFillColorSecondary : null,
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                      child: Row(children: [
                        for (var k = 0; k < fileRows[i].length; k++)
                          SizedBox(
                            width: const [90.0, 160.0, 120.0, 140.0, 140.0, 80.0][k],
                            child: Padding(
                              padding: EdgeInsets.only(right: k == 2 ? 16 : 8),
                              child: Text(
                                i > 0 && k == 2 && fileRows[i][k] is num ? won(fileRows[i][k] as num) : '${fileRows[i][k] ?? ''}',
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                textAlign: k == 2 ? TextAlign.right : TextAlign.left,
                                style: TextStyle(fontWeight: i == 0 ? FontWeight.w600 : null, fontSize: 12.5),
                              ),
                            ),
                          ),
                      ]),
                    ),
                ]),
              ),
            ),
          ),
        ]),
      ),
      SidePanel(width: 340, children: [
        SectionTitle('다운로드', FluentIcons.download, Tone.green),
        if (c.bad > 0)
          Text('오류 ${c.bad}건이 남아 있어요. 2단계에서 고친 뒤 받을 수 있어요.', style: TextStyle(color: Tone.bad(context), fontWeight: FontWeight.w600))
        else if (c.warn > 0)
          Checkbox(checked: m.ack, onChanged: (v) => m.setAck(v ?? false), content: Text('확인 필요 ${c.warn}건을 원문과 대조했어요', style: TextStyle(color: Tone.warn(context))))
        else
          Text('오류·확인 필요 없음', style: TextStyle(color: Tone.ok(context), fontWeight: FontWeight.w600)),
        const SizedBox(height: 10),
        FilledButton(onPressed: m.canDownload ? m.download : null, child: const _BtnText(FluentIcons.download, '하나은행 이체파일 다운로드', '')),
        if (m.downloadedSig.isNotEmpty && m.downloadedSig == m.dataSig)
          Padding(padding: const EdgeInsets.only(top: 6), child: Text('지금 내용으로 받았어요.', style: TextStyle(color: Tone.ok(context), fontSize: 12.5))),
        const SizedBox(height: 20),
        SectionTitle('송금 코멘트', FluentIcons.chat, Tone.blue,
            trailing: Button(onPressed: comments.isEmpty ? null : () => m.copyComments(comments), child: const Text('모두 복사'))),
        for (final line in comments)
          Padding(
            padding: const EdgeInsets.only(bottom: 6),
            child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Expanded(child: SelectableText(line, style: const TextStyle(fontSize: 12.5, height: 1.4))),
              IconButton(icon: const Icon(FluentIcons.copy, size: 13), onPressed: () => Ui.copy(line)),
            ]),
          ),
      ]),
    ]);
  }
}

class _BtnText extends StatelessWidget {
  const _BtnText(this.icon, this.text, this.hint);
  final IconData icon;
  final String text, hint;
  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 4, horizontal: 4),
        child: Row(mainAxisSize: MainAxisSize.min, children: [
          Icon(icon, size: 13),
          const SizedBox(width: 8),
          Text(text, style: const TextStyle(fontWeight: FontWeight.w600)),
          if (hint.isNotEmpty) ...[const SizedBox(width: 8), Text(hint, style: const TextStyle(fontSize: 11.5))],
        ]),
      );
}

/// 2단계 표의 편집 칸. 금액은 쉼표를 넣어 보여주고, 고칠 때는 숫자만 보여요.
class _CellBox extends StatefulWidget {
  const _CellBox({super.key, required this.model, required this.row, required this.field, required this.revision, required this.onFocus, this.tone, this.right = false});
  final TransferModel model;
  final TRow row;
  final String field;
  final int revision;
  final VoidCallback onFocus;
  final Color? tone;
  final bool right;
  @override
  State<_CellBox> createState() => _CellBoxState();
}

class _CellBoxState extends State<_CellBox> {
  late final TextEditingController c = TextEditingController(text: _display());
  final focus = FocusNode();

  String get _raw => widget.model.value(widget.row, widget.field);
  String _display() {
    final v = _raw;
    if (widget.field == 'amount' && RegExp(r'^\d+$').hasMatch(v)) return won(int.parse(v));
    return v;
  }

  @override
  void initState() {
    super.initState();
    focus.addListener(() {
      if (focus.hasFocus) {
        widget.onFocus();
        if (widget.field == 'amount') c.text = _raw;
      } else {
        c.text = _display();
      }
    });
  }

  @override
  void didUpdateWidget(covariant _CellBox old) {
    super.didUpdateWidget(old);
    if (!focus.hasFocus) {
      final d = _display();
      if (c.text != d) c.text = d;
    }
  }

  @override
  void dispose() {
    c.dispose();
    focus.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => TextBox(
        controller: c,
        focusNode: focus,
        textAlign: widget.right ? TextAlign.right : TextAlign.left,
        style: TextStyle(fontSize: 13, color: widget.tone, fontWeight: widget.field == 'plate' ? FontWeight.w600 : null),
        decoration: widget.tone == null ? null : WidgetStatePropertyAll(BoxDecoration(border: Border.all(color: widget.tone!.withValues(alpha: 0.7)), borderRadius: BorderRadius.circular(4))),
        onChanged: (v) => widget.model.edit(widget.row.id, widget.field, v),
      );
}
