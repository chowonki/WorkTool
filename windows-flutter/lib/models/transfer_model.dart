import 'dart:math';

import 'package:fluent_ui/fluent_ui.dart';

import '../core/rules.dart';
import '../core/util.dart';
import '../ui/ui.dart';

/// 이체 한 건 (웹 transfer.html의 rows 항목과 같은 모양)
class TRow {
  TRow(this.id, {this.bank = '', this.account = '', this.amount = '', this.plate = '', this.outMemo = '', this.inMemo = '', this.cms = ''});
  TRow.fromJson(Json j)
      : id = j['id'] as String,
        bank = j['bank'] as String? ?? '',
        account = j['account'] as String? ?? '',
        amount = j['amount'] as String? ?? '',
        plate = j['plate'] as String? ?? '',
        outMemo = j['outMemo'] as String? ?? '',
        inMemo = j['inMemo'] as String? ?? '',
        cms = j['cms'] as String? ?? '',
        extra = {for (final e in j.entries) if (!_fields.contains(e.key)) e.key: e.value};

  static const _fields = {'id', 'bank', 'account', 'amount', 'plate', 'outMemo', 'inMemo', 'cms'};
  final String id;
  String bank, account, amount, plate, outMemo, inMemo, cms;

  /// 원문·라벨·스레드·분할 정보처럼 화면에서 고치지 않는 값
  Json extra = {};

  String get ctx => extra['ctx'] as String? ?? extra['source'] as String? ?? '';
  String get label => extra['label'] as String? ?? '';
  String? get splitGroup => extra['splitGroup'] as String?;
  int get thread => (extra['thread'] as num?)?.toInt() ?? 0;
  String get holder => extra['holder'] as String? ?? '';

  Json toJson() => {...extra, 'id': id, 'bank': bank, 'account': account, 'amount': amount, 'plate': plate, 'outMemo': outMemo, 'inMemo': inMemo, 'cms': cms};

  String get sig => [bank, account, amount, outMemo, inMemo, cms, plate].join('\u001f');
}

class TThread {
  TThread(this.id);
  final String id;
  final text = TextEditingController();
  String imported = '';
}

class TCheck {
  const TCheck([this.state = 'ok', this.errors = const [], this.reviews = const []]);
  final String state;
  final List<String> errors, reviews;
}

class TransferModel extends ChangeNotifier {
  TransferModel() {
    threads = [TThread(newId())];
  }

  final rules = Rules.shared;
  int step = 1;
  late List<TThread> threads;
  List<TRow> rows = [];
  Map<String, TCheck> checks = {};
  Map<String, String> splitLabels = {};
  String filter = 'all';
  Set<String> selection = {};
  String? focused;
  final cmsAll = TextEditingController();
  final outMemoAll = TextEditingController();
  final inMemoAll = TextEditingController();
  bool onlyEmpty = true;
  bool autoFix = false;
  String extractInfo = '';
  String extractedAt = '';
  bool ack = false;
  String _ackSig = '';
  String downloadedSig = '';

  /// 셀 편집기가 다시 그려야 할 때 올려요 (값을 코드에서 바꿨을 때)
  int revision = 0;

  String newId() {
    try {
      return rules.transferCall('Transfer.newId()', []) as String;
    } catch (_) {
      return '${DateTime.now().microsecondsSinceEpoch}${Random().nextInt(1 << 20)}';
    }
  }

  List<Json> _json(List<TRow> list) => [for (final r in list) r.toJson()];
  List<TRow> _decode(dynamic list) => [for (final x in list as List? ?? []) TRow.fromJson(Json.from(x as Map))];

  void changed({bool external = false}) {
    _recheck();
    if (external) revision++;
    notifyListeners();
  }

  void _recheck() {
    final js = _json(rows);
    List res = [], labels = [];
    try {
      res = rules.transferCall('Transfer.check(__rows)', js) as List;
      labels = rules.transferCall('Transfer.splitLabel(__rows)', js) as List;
    } catch (_) {}
    final c = <String, TCheck>{}, l = <String, String>{};
    for (var i = 0; i < rows.length; i++) {
      if (i < res.length) {
        final m = res[i] as Map;
        c[rows[i].id] = TCheck(m['state'] as String? ?? 'ok', List<String>.from(m['errors'] as List? ?? []), List<String>.from(m['reviews'] as List? ?? []));
      }
      if (i < labels.length && (labels[i] as String? ?? '').isNotEmpty) l[rows[i].id] = labels[i] as String;
    }
    checks = c;
    splitLabels = l;
    if (ack && _ackSig != dataSig) {
      ack = false;
      _ackSig = '';
    }
  }

  // MARK: 통계

  ({int all, int ok, int warn, int bad, double sum}) get counts {
    var ok = 0, warn = 0, bad = 0;
    var sum = 0.0;
    for (final r in rows) {
      switch (checks[r.id]?.state ?? 'ok') {
        case 'bad':
          bad++;
        case 'warn':
          warn++;
        default:
          ok++;
      }
      final a = double.tryParse(r.amount);
      if (a != null && a > 0 && a == a.roundToDouble()) sum += a;
    }
    return (all: rows.length, ok: ok, warn: warn, bad: bad, sum: sum);
  }

  List<TRow> get visibleRows => filter == 'all' ? rows : rows.where((r) => (checks[r.id]?.state ?? 'ok') == filter).toList();

  String get dataSig => rows.map((r) => r.sig).join('\u001e');
  bool get hasUnsaved => rows.isNotEmpty && downloadedSig != dataSig;

  void setAck(bool on) {
    ack = on;
    _ackSig = on ? dataSig : '';
    notifyListeners();
  }

  void setStep(int n) {
    if (n == step) return;
    if (n >= 2 && rows.isEmpty) {
      Ui.toast('먼저 이체정보를 추출해 주세요.', error: true);
      return;
    }
    if (n == 3) {
      goStep3();
      return;
    }
    step = n;
    notifyListeners();
  }

  // MARK: 1단계: 추출

  void addThread() {
    threads = [...threads, TThread(newId())];
    notifyListeners();
  }

  void removeThread(TThread t) {
    if (threads.length > 1) {
      threads = threads.where((x) => x != t).toList();
    } else {
      threads[0].text.clear();
    }
    notifyListeners();
  }

  String _trimmed(String s) => s.replaceAll('\r\n', '\n').replaceAll('\r', '\n').trim();

  /// 추출 결과. 차량번호가 없는 건이 있으면 askPlates로 돌려줘요.
  Future<void> extract(Future<void> Function(List<TRow> noPlate, List<String> warnings) askPlates) async {
    if (!threads.any((t) => t.text.text.trim().isNotEmpty)) {
      Ui.toast('스레드 내용을 먼저 붙여넣어 주세요.', error: true);
      return;
    }
    final pending = [for (var i = 0; i < threads.length; i++) if (_trimmed(threads[i].text.text).isNotEmpty && threads[i].imported != _trimmed(threads[i].text.text)) i];
    if (pending.isEmpty) {
      Ui.toast('새로 넣거나 바뀐 스레드가 없어요. 입력칸을 추가해 새 내용을 넣어 주세요.', error: true);
      return;
    }
    final prevImported = [for (final i in pending) threads[i].imported];
    final added = <Json>[];
    final warnings = <String>[], notes = <String>[];
    final seenRaw = <String, int>{};
    for (var i = 0; i < threads.length; i++) {
      final r = _trimmed(threads[i].text.text);
      if (threads[i].imported.isNotEmpty && threads[i].imported == r && !seenRaw.containsKey(r)) seenRaw[r] = i + 1;
    }
    for (final i in pending) {
      final raw = _trimmed(threads[i].text.text);
      final same = seenRaw[raw];
      if (same != null) {
        warnings.add('이체 요청 ${i + 1}: 이체 요청 $same와 내용이 같아 건너뛰었어요.');
        threads[i].imported = raw;
        continue;
      }
      seenRaw[raw] = i + 1;
      Map res;
      try {
        res = rules.transferCall('(Transfer.setRows(__rows),Transfer.extractThread(__raw,__i,__tid,__cms))', _json(rows),
            {'__raw': raw, '__i': i, '__tid': threads[i].id, '__cms': cmsAll.text.trim()}) as Map;
      } catch (e) {
        warnings.add('이체 요청 ${i + 1}: $e');
        continue;
      }
      if (res['empty'] == true) {
        warnings.add('이체 요청 ${i + 1}: 이체정보를 찾지 못했어요.');
        continue;
      }
      added.addAll([for (final x in res['added'] as List? ?? []) Json.from(x as Map)]);
      final skipped = (res['skipped'] as num?)?.toInt() ?? 0, fresh = (res['fresh'] as num?)?.toInt() ?? 0;
      if (skipped > 0) notes.add('이체 요청 ${i + 1}: 이미 추출한 $skipped건은 다시 넣지 않았어요${fresh > 0 ? ' (새 $fresh건 추가)' : ''}.');
      threads[i].imported = raw;
    }
    final before = added.length;
    final split = _decode(rules.transferCall('Transfer.splitAll(__rows)', added));
    final nSplit = split.length - before;
    if (split.isEmpty) {
      final msg = [...notes, ...warnings].join('\n');
      await Ui.alert('추출 결과', msg.isEmpty ? '추출할 이체정보가 없어요.' : msg);
      notifyListeners();
      return;
    }
    if (rows.length + split.length > Rules.transferMax) {
      await Ui.alert('최대 건수 초과',
          '기존 ${rows.length}건 + 새 ${split.length}건 = ${rows.length + split.length}건이에요.\n한 파일에 ${Rules.transferMax}건까지 만들 수 있어요. 나눠서 처리해 주세요.');
      for (var k = 0; k < pending.length; k++) {
        threads[pending[k]].imported = prevImported[k];
      }
      return;
    }
    rows = [...rows, ...split];
    extractedAt = stamp('yyyy. M. d. HH:mm');
    extractInfo = '새 ${split.length}건 추가 · 총 ${rows.length}건';
    step = 2;
    changed(external: true);
    final noPlate = split.where((r) => r.plate.isEmpty && (r.splitGroup == null || split.firstWhere((x) => x.splitGroup == r.splitGroup).id == r.id)).toList();
    if (noPlate.isNotEmpty) {
      await askPlates(noPlate, warnings);
    } else if (warnings.isNotEmpty) {
      await Ui.alert('확인해 주세요', warnings.join('\n'));
    } else {
      Ui.toast(notes.isEmpty ? '${split.length}건을 추출했어요. 원문과 맞는지 확인해 주세요.' : notes.join(' '));
    }
    if (nSplit > 0) {
      Future.delayed(const Duration(seconds: 3), () => Ui.toast('1억 넘는 금액을 나눠 $nSplit줄을 더 만들었어요. ‘분할’ 표시를 확인해 주세요.'));
    }
  }

  List<String> groupIds(String id) {
    final r = rows.where((x) => x.id == id).firstOrNull;
    if (r == null) return [];
    final g = r.splitGroup;
    if (g == null) return [id];
    return [for (final x in rows) if (x.splitGroup == g) x.id];
  }

  bool isPlate(String v) {
    try {
      return rules.transferCall('Transfer.plateOK(__v)', [], {'__v': v}) == true;
    } catch (_) {
      return false;
    }
  }

  /// 차량번호가 없던 건에 입력한 값을 반영해요. 형식이 틀리면 그 차량번호를 돌려줘요.
  String? applyPlates(List<({String id, bool usePlate, String text})> inputs) {
    for (final x in inputs.where((x) => x.usePlate)) {
      final v = x.text.replaceAll(RegExp(r'\s'), '');
      if (v.isNotEmpty && !isPlate(v)) return v;
    }
    for (final x in inputs) {
      final ids = groupIds(x.id).toSet();
      if (x.usePlate) {
        final v = x.text.replaceAll(RegExp(r'\s'), '');
        if (v.isEmpty) continue;
        for (final r in rows.where((r) => ids.contains(r.id))) {
          r.plate = v;
          if (r.outMemo.isEmpty) r.outMemo = v;
          if (r.inMemo.isEmpty) r.inMemo = v;
        }
      } else {
        final v = x.text.trim();
        if (v.isEmpty) continue;
        for (final r in rows.where((r) => ids.contains(r.id))) {
          r.inMemo = v;
          if (r.outMemo.isEmpty) r.outMemo = v;
        }
      }
    }
    changed(external: true);
    Ui.toast('입력한 내용을 반영했어요.');
    return null;
  }

  // MARK: 2단계: 확인·수정

  String value(TRow r, String field) => switch (field) {
        'bank' => r.bank,
        'account' => r.account,
        'amount' => r.amount,
        'plate' => r.plate,
        'outMemo' => r.outMemo,
        'inMemo' => r.inMemo,
        _ => r.cms,
      };

  /// 셀 값을 바꿔요. 차량번호를 바꾸면 비어 있거나 같던 메모도 따라 바꿔요. 다른 칸이 바뀌면 true.
  bool edit(String id, String field, String v) {
    final r = rows.where((x) => x.id == id).firstOrNull;
    if (r == null) return false;
    var other = false;
    switch (field) {
      case 'bank':
        r.bank = v;
      case 'account':
        r.account = autoFix ? v.replaceAll(RegExp(r'[ -]'), '') : v;
        other = r.account != v;
      case 'amount':
        r.amount = v.replaceAll(',', '').replaceAll('원', '').trim();
      case 'plate':
        final old = r.plate;
        if (r.outMemo.isEmpty || r.outMemo == old) {
          r.outMemo = v;
          other = true;
        }
        if (r.inMemo.isEmpty || r.inMemo == old) {
          r.inMemo = v;
          other = true;
        }
        r.plate = v;
      case 'outMemo':
        r.outMemo = v;
      case 'inMemo':
        r.inMemo = v;
      case 'cms':
        r.cms = v;
    }
    changed(external: other);
    return other;
  }

  void setAutoFix(bool on) {
    autoFix = on;
    if (on) {
      for (final r in rows) {
        r.account = r.account.replaceAll(RegExp(r'[ -]'), '');
      }
    }
    changed(external: true);
  }

  void addRow() {
    if (rows.length >= Rules.transferMax) {
      Ui.toast('최대 ${Rules.transferMax}건까지예요.', error: true);
      return;
    }
    final r = TRow(newId(), cms: cmsAll.text.trim());
    filter = 'all';
    rows = [...rows, r];
    selection = {r.id};
    focused = r.id;
    changed(external: true);
  }

  Future<void> deleteSelected() async {
    if (selection.isEmpty) {
      Ui.toast('삭제할 줄을 선택해 주세요.', error: true);
      return;
    }
    final n = selection.length;
    if (!await Ui.confirm('선택한 $n건을 삭제할까요?', '삭제한 줄은 되돌릴 수 없어요.', ok: '삭제', destructive: true)) return;
    rows = rows.where((r) => !selection.contains(r.id)).toList();
    selection = {};
    if (focused != null && !rows.any((r) => r.id == focused)) focused = null;
    changed(external: true);
    Ui.toast('$n건을 삭제했어요.');
  }

  void applyMemo(String field, String value, String title) {
    if (rows.isEmpty) {
      Ui.toast('적용할 이체 내역이 없어요.', error: true);
      return;
    }
    var n = 0;
    for (final r in rows) {
      final cur = this.value(r, field);
      if (onlyEmpty && cur.isNotEmpty) continue;
      switch (field) {
        case 'outMemo':
          r.outMemo = value;
        case 'inMemo':
          r.inMemo = value;
        case 'cms':
          r.cms = value;
      }
      n++;
    }
    changed(external: true);
    Ui.toast('$title을(를) $n건에 적용했어요.');
  }

  void goStep3() {
    final c = counts;
    if (c.all == 0) {
      Ui.toast('이체 내역이 없어요.', error: true);
      return;
    }
    step = 3;
    notifyListeners();
    if (c.bad > 0) Ui.toast('오류 ${c.bad}건이 남아 있어요. 고친 뒤 받을 수 있어요.', error: true);
  }

  // MARK: 3단계: 다운로드

  List<List<dynamic>> get fileRows {
    try {
      return [for (final r in rules.transferCall('Transfer.fileRows(__rows)', _json(rows)) as List) List<dynamic>.from(r as List)];
    } catch (_) {
      return [];
    }
  }

  List<String> get comments {
    try {
      return List<String>.from(rules.transferCall('Transfer.comments(__rows)', _json(rows)) as List);
    } catch (_) {
      return [];
    }
  }

  bool get canDownload {
    final c = counts;
    return c.all > 0 && c.bad == 0 && c.all <= Rules.transferMax && (c.warn == 0 || ack);
  }

  Future<void> download() async {
    if (!canDownload) return;
    final js = _json(rows);
    final keys = List<String>.from(rules.transferCall('Transfer.sentKeys(__rows)', js) as List);
    final sent = SentLog.load();
    final again = [for (var k = 0; k < keys.length; k++) if (sent.containsKey(keys[k])) (row: k, at: sent[keys[k]]!)];
    if (again.isNotEmpty && downloadedSig != dataSig) {
      final lines = [
        for (final m in again.take(10)) '${rows[m.row].bank} ${rows[m.row].account} ${won(double.tryParse(rows[m.row].amount) ?? 0)}원 · ${m.at}에 받음',
        if (again.length > 10) '외 ${again.length - 10}건',
      ];
      if (!await Ui.confirm('오늘 이미 받은 이체 건이 있어요', '같은 은행·계좌·금액 ${again.length}건이 오늘 받은 이체파일에 이미 들어 있어요. 두 번 송금하지 않도록 확인해 주세요.',
          ok: '그래도 받기', details: lines.join('\n'))) {
        return;
      }
    }
    final now = DateTime.now();
    try {
      final name = rules.transferCall('Transfer.fileName(__rows,__stamp)', js, {'__stamp': stamp('yyyyMMdd_HHmm', now)}) as String? ??
          '하나은행_다계좌이체_${stamp('yyyyMMdd_HHmm', now)}.xlsx';
      final data = await rules.transferXlsx(js);
      if (await Ui.save(data, name, 'Excel 통합 문서', ['xlsx']) == null) return;
      downloadedSig = dataSig;
      SentLog.remember(keys, stamp('HH:mm', now));
      notifyListeners();
      Ui.toast('이체파일을 저장했어요. 은행 업로드 전에 한 번 더 확인해 주세요.');
    } catch (e) {
      await Ui.alert('파일을 만들지 못했어요', '입력 내용은 그대로 남아 있어요. 잠시 뒤 다시 눌러 주세요.\n$e');
    }
  }

  Future<void> copyComments(List<String> lines) async {
    if (lines.isEmpty) {
      Ui.toast('이체 내역이 없어요.', error: true);
      return;
    }
    await Ui.copy(lines.join('\n'));
  }

  Future<void> resetAll() async {
    if (hasUnsaved && !await Ui.confirm('처음부터 다시 할까요?', '받지 않은 이체 내역 ${rows.length}건이 지워져요.', ok: '지우기', destructive: true)) return;
    rows = [];
    threads = [TThread(newId())];
    step = 1;
    filter = 'all';
    selection = {};
    focused = null;
    extractInfo = '';
    extractedAt = '';
    ack = false;
    _ackSig = '';
    downloadedSig = '';
    changed(external: true);
  }
}

/// 오늘 받은 이체 지문(은행·계좌·금액을 섞은 값). 계좌번호 원문은 남기지 않아요. 날짜가 바뀌면 지워요.
class SentLog {
  static const key = 'transfer-sent-today-v1';
  static String today() => stamp('yyyy-MM-dd');

  static Map<String, String> load() {
    final d = Store.shared[key];
    if (d is! Map || d['date'] != today()) {
      Store.shared[key] = null;
      return {};
    }
    return {for (final x in d['items'] as List? ?? []) (x as Map)['key'] as String: x['at'] as String? ?? ''};
  }

  static void remember(List<String> keys, String at) {
    final d = Store.shared[key];
    final items = d is Map && d['date'] == today() ? [for (final x in d['items'] as List? ?? []) Map<String, dynamic>.from(x as Map)] : <Map<String, dynamic>>[];
    final have = {for (final x in items) x['key']};
    for (final k in keys) {
      if (have.add(k)) items.add({'key': k, 'at': at});
    }
    Store.shared[key] = {'date': today(), 'items': items.length > 2000 ? items.sublist(items.length - 2000) : items};
  }
}
