import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:fluent_ui/fluent_ui.dart';

import '../core/exports.dart';
import '../core/rules.dart';
import '../core/util.dart';
import '../ui/ui.dart';

/// 조회 결과 한 줄 (웹 DispatchCore.match 결과 그대로 + 표시용 값)
class DRow {
  DRow(this.id, this.raw);
  final int id;
  Json raw;

  String str(String k) => raw[k] as String? ?? '';
  double? numOf(String k) => (raw[k] as num?)?.toDouble();
  bool flag(String k) => raw[k] == true;

  String get plate => str('plate');
  String get schedule => str('schedule');
  String get status => str('status');
  String get label => str('_label');
  String get notes => str('_notes');
  String get vendor => str('vendor');
  double? get amount => numOf('amount');
  double? get fee => numOf('dispatchFee');
  double? get vat => numOf('vat');
  bool get progress => flag('_progress');
  bool get unresolved => flag('_unresolved');
  Set<String> get filters => {...List<String>.from(raw['_filters'] as List? ?? [])};
  List<String> get rawStates => List<String>.from(raw['rawStates'] as List? ?? []);

  List<({String id, String value, String vendor, String source, String state})> candidates(String field) => [
        for (final c in raw[field == 'schedule' ? 'scheduleCandidates' : 'feeCandidates'] as List? ?? [])
          (
            id: (c as Map)['id'] as String? ?? '',
            value: c['value'] is num ? '${won(c['value'] as num)}원' : (c['value'] as String? ?? ''),
            vendor: vendorName(c['vendor'] as String? ?? ''),
            source: c['source'] as String? ?? '',
            state: c['state'] as String? ?? '',
          ),
      ];
}

/// 업체 파일 한 개
class VendorFile {
  VendorFile(this.key, this.name, this.encrypted, this.result);
  final String key, name;
  final bool encrypted;
  final Json result;
  int get records => (result['records'] as List?)?.length ?? 0;
  String get vendors {
    final v = {for (final r in result['records'] as List? ?? []) (r as Map)['vendor'] as String? ?? ''}..remove('');
    return (v.toList()..sort()).map(vendorName).join('·');
  }

  int get ignored => (result['ignored'] as num?)?.toInt() ?? 0;
}

class DispatchModel extends ChangeNotifier {
  DispatchModel() {
    input.addListener(_onInput);
    final d = Store.shared['dispatch-marks'];
    if (d is Map && d['date'] == stamp('yyyy-MM-dd')) {
      completed = {...List<String>.from(d['done'] as List? ?? [])};
      copied = {...List<String>.from(d['copied'] as List? ?? [])};
    }
  }

  static const filterTitles = [
    ('all', '전체'), ('review', '확인 필요'), ('confirmed', '금액 확인'), ('progress', '진행 중'), ('missing', '미조회'),
    ('jeju', '제주 출발'), ('carrier', '캐리어탁송'), ('slack', '슬랙봇 문의'), ('slack-replies', '슬랙 회신 반영'),
  ];
  static const scopes = [('all', '전체 조회 결과'), ('confirmed', '금액 확인 차량'), ('visible', '현재 표시 목록')];
  static const pageSize = 100;

  final rules = Rules.shared;
  final input = TextEditingController();
  final search = TextEditingController();
  final slackText = TextEditingController();

  ParsedInput? parsed;
  List<VendorFile> files = [];
  List<DRow> results = [];
  bool stale = false;
  String filter = 'all';
  String vendorFilter = 'all';
  String stateFilter = 'all';
  Set<int> selection = {};
  Set<String> completed = {};
  Set<String> copied = {};
  String scope = 'all';
  bool busy = false;
  String status = '차량목록과 업체 파일을 넣고 조회해 주세요.';
  String sortKey = '';
  bool sortAsc = true;
  int page = 0;
  String slackReport = '';

  String _lastInput = '';
  bool _cleaning = false;

  void _onInput() {
    if (_cleaning) return;
    final text = input.text;
    if (text == _lastInput) return;
    // 붙여넣기처럼 한 번에 여러 글자가 들어오면 빈 줄을 알아서 지워요. 한 글자씩 칠 때나 지울 때는 그대로 둬요.
    if (text.length - _lastInput.length > 1) {
      final cleaned = dropBlankLines(text);
      if (cleaned != text) {
        _cleaning = true;
        input.value = TextEditingValue(text: cleaned, selection: TextSelection.collapsed(offset: cleaned.length));
        _cleaning = false;
      }
    }
    _lastInput = input.text;
    _parse();
    notifyListeners();
  }

  void _parse() {
    final t = input.text;
    try {
      parsed = t.trim().isEmpty ? null : rules.parseInput(t);
    } catch (_) {
      parsed = null;
    }
    if (results.isNotEmpty) stale = true;
  }

  void _saveMarks() => Store.shared['dispatch-marks'] = {'date': stamp('yyyy-MM-dd'), 'done': completed.toList(), 'copied': copied.toList()};

  void update(VoidCallback f) {
    f();
    notifyListeners();
  }

  // MARK: 파일 첨부

  Future<void> addFiles(List<String> paths) async {
    if (busy || paths.isEmpty) return;
    update(() => busy = true);
    var added = 0;
    final errors = <String>[], layout = <String>[];
    for (final path in paths) {
      final f = File(path);
      final name = path.split(RegExp(r'[\\/]')).last;
      if (!name.toLowerCase().endsWith('.xlsx')) {
        errors.add('$name: .xlsx 형식만 지원해요. (.xls는 엑셀에서 .xlsx로 저장해 주세요)');
        continue;
      }
      try {
        final st = f.statSync();
        if (st.size > 40 * 1024 * 1024) {
          errors.add('$name: 파일은 40MB까지 지원해요.');
          continue;
        }
        final key = '$name/${st.size}/${st.modified.millisecondsSinceEpoch}';
        if (files.any((x) => x.key == key)) {
          errors.add('$name: 이미 첨부한 파일이에요.');
          continue;
        }
        update(() => status = '$name 읽는 중…');
        final data = await f.readAsBytes();
        final encrypted = Rules.isEncryptedExcel(data);
        String? pw;
        if (encrypted) {
          pw = await Ui.password('암호가 설정된 파일', name);
          if (pw == null) continue;
        }
        final result = await rules.readVendorFile(data, name, pw);
        pw = null;
        files = [...files, VendorFile(key, name, encrypted, result)];
        if (results.isNotEmpty) stale = true;
        added++;
        for (final w in result['warnings'] as List? ?? []) {
          layout.add('$name\n$w');
        }
      } catch (e) {
        errors.add('$name\n$e');
      }
    }
    final total = files.fold(0, (a, f) => a + f.records);
    update(() {
      busy = false;
      status = '$added개 파일 추가 · 총 ${files.length}개 파일 / ${won(total)}개 기록';
    });
    if (errors.isNotEmpty) await Ui.alert('첨부 결과 확인', '${errors.length}개 항목을 확인해 주세요. 다른 파일은 정상 첨부돼요.', details: errors.join('\n\n'));
    if (layout.isNotEmpty) {
      await Ui.alert('업체 양식이 바뀌었을 수 있어요',
          '파일은 첨부했지만 일부 열의 값이 평소와 달라요. 업체 사이트에서 받은 원본 시트인지, 열이 추가·이동되지 않았는지 확인한 뒤 조회해 주세요.',
          details: layout.join('\n\n'));
    }
  }

  void removeFile(VendorFile f) => update(() {
        files = files.where((x) => x != f).toList();
        if (results.isNotEmpty) stale = true;
      });

  // MARK: 조회

  bool get canLookup => !busy && (parsed?.vehicles.isNotEmpty ?? false) && files.isNotEmpty;

  Future<void> lookup() async {
    if (busy) return;
    final tidy = dropBlankLines(input.text).replaceAll(RegExp(r'^\n+|\n+$'), '');
    if (tidy != input.text) {
      _cleaning = true;
      input.text = tidy;
      _cleaning = false;
      _lastInput = tidy;
    }
    final ParsedInput p;
    try {
      p = rules.parseInput(input.text);
    } catch (e) {
      await Ui.alert('차량목록을 읽지 못했어요', '$e');
      return;
    }
    parsed = p;
    if (p.invalid.isNotEmpty) {
      await Ui.alert('차량목록을 확인해 주세요', '인식하지 못한 ${p.invalid.length}줄이 있어요. 아래 줄을 수정한 뒤 다시 조회해 주세요.', details: p.invalid.take(30).join('\n'));
      return;
    }
    if (p.vehicles.isEmpty || files.isEmpty) {
      Ui.toast('차량목록과 업체 파일을 모두 넣어 주세요.', error: true);
      return;
    }
    try {
      final rows = rules.match(p.vehicles, [for (final f in files) f.result]);
      update(() {
        results = [for (var i = 0; i < rows.length; i++) DRow(i, rows[i])];
        selection = {};
        stale = false;
        slackText.clear();
        slackReport = '';
        page = 0;
        if (filter == 'slack-replies') filter = 'all';
        status = '조회 완료 · 차량번호 $distinctCount대 · 입력 중복 ${p.duplicates}건';
      });
      Ui.toast('차량 조회를 마쳤어요.');
    } catch (e) {
      update(() => status = '조회 오류: $e');
      await Ui.alert('차량 조회 오류', '$e');
    }
  }

  // MARK: 표시

  List<String> get stateOptions => ({for (final r in results) ...r.rawStates}.toList()..sort());
  List<String> get vendorOptions => ({for (final r in results) r.vendor}..remove('')).toList()..sort();

  List<DRow> get visible {
    final q = search.text.replaceAll(' ', '');
    final rows = results.where((r) =>
        r.filters.contains(filter) &&
        (q.isEmpty || r.plate.contains(q)) &&
        (vendorFilter == 'all' || r.vendor == vendorFilter) &&
        (stateFilter == 'all' || r.rawStates.contains(stateFilter))).toList();
    if (sortKey.isNotEmpty) {
      int cmp(DRow a, DRow b) {
        switch (sortKey) {
          case 'schedule':
            return (a.schedule.isEmpty ? '~' : a.schedule).compareTo(b.schedule.isEmpty ? '~' : b.schedule);
          case 'plate':
            return a.plate.compareTo(b.plate);
          case 'amount':
            return (a.amount ?? -1).compareTo(b.amount ?? -1);
          case 'fee':
            return (a.fee ?? -1).compareTo(b.fee ?? -1);
          case 'vat':
            return (a.vat ?? -1).compareTo(b.vat ?? -1);
          case 'label':
            return a.label.compareTo(b.label);
          case 'vendor':
            return a.vendor.compareTo(b.vendor);
        }
        return 0;
      }

      rows.sort((a, b) => sortAsc ? cmp(a, b) : cmp(b, a));
    }
    return rows;
  }

  void sortBy(String key) => update(() {
        if (sortKey == key) {
          if (sortAsc) {
            sortAsc = false;
          } else {
            sortKey = '';
          }
        } else {
          sortKey = key;
          sortAsc = true;
        }
      });

  int count(String f) => {for (final r in results) if (r.filters.contains(f)) r.plate}.length;
  int get distinctCount => {for (final r in results) r.plate}.length;

  void setFilter(String f) => update(() {
        filter = f;
        page = 0;
        selection = {};
      });

  List<DRow>? get picked => selection.isEmpty ? null : results.where((r) => selection.contains(r.id)).toList();

  List<Json> get scopeRows {
    final rows = picked ?? (scope == 'confirmed' ? results.where((r) => r.status == 'confirmed').toList() : scope == 'visible' ? visible : results);
    return rules.unique([for (final r in rows) r.raw]);
  }

  String get scopeLabel => picked != null ? '선택한 줄' : scopes.firstWhere((s) => s.$1 == scope).$2;

  // MARK: 고르기·완료

  void choose(DRow row, String field, String id) {
    try {
      row.raw = rules.useCandidate(row.raw, field, id);
      notifyListeners();
    } catch (e) {
      Ui.alert('선택하지 못했어요', '$e');
    }
  }

  void toggleDone(String plate) => update(() {
        if (!completed.remove(plate)) completed.add(plate);
        _saveMarks();
      });

  void toggleSelect(int id) => update(() {
        if (!selection.remove(id)) selection.add(id);
      });

  // MARK: 복사·저장

  Future<void> copyPlates() async {
    final rows = rules.unique([for (final r in picked ?? visible) r.raw]);
    if (rows.isEmpty) {
      Ui.toast('현재 목록에 복사할 차량이 없어요.', error: true);
      return;
    }
    final plates = [for (final r in rows) r['plate'] as String? ?? ''];
    String text;
    if (filter == 'slack') {
      final jeju = [for (final r in rows) if (r['jeju'] == true) r['plate'] as String];
      final normal = [for (final r in rows) if (r['jeju'] != true) r['plate'] as String];
      text = [('제주도출발차량', jeju), ('일반차량', normal)].where((g) => g.$2.isNotEmpty).map((g) => '[${g.$1}]\n${g.$2.join('\n')}').join('\n\n');
    } else {
      text = plates.join('\n');
    }
    await Ui.copy(text);
    update(() {
      copied.addAll(plates);
      _saveMarks();
    });
  }

  Future<void> copyCell(String value) async {
    if (value.isEmpty || value == '—') return;
    await Ui.copy(value);
  }

  Future<bool> _guardExport(List<Json> rows) async {
    if (results.isEmpty) {
      Ui.toast('먼저 차량 조회를 해 주세요.', error: true);
      return false;
    }
    if (rows.isEmpty) {
      Ui.toast('선택한 범위에 차량이 없어요.', error: true);
      return false;
    }
    final pending = rows.where((r) => r['_unresolved'] == true).toList();
    if (pending.isNotEmpty) {
      final lines = pending.take(25).map((r) {
        final what = [if (r['scheduleConflict'] == true) '일정', if (r['feeConflict'] == true) '탁송비용'].join(', ');
        return '${r['plate']} · $what';
      });
      await Ui.alert('중첩 일정·탁송비용 선택 필요', '${pending.length}대의 일정 또는 탁송비용을 먼저 선택해 주세요. 선택하지 않은 값을 임의로 저장하지 않아요.',
          details: lines.join('\n'));
      return false;
    }
    return true;
  }

  Future<void> capture({required bool copy}) async {
    final rows = scopeRows;
    if (results.isEmpty) {
      Ui.toast('먼저 차량 조회를 해 주세요.', error: true);
      return;
    }
    if (rows.isEmpty) {
      Ui.toast('선택한 범위에 차량이 없어요.', error: true);
      return;
    }
    if (rows.length > DispatchExports.captureMax) {
      await Ui.alert('캡처할 차량이 너무 많아요',
          '이미지 한 장에는 ${DispatchExports.captureMax}대까지 담을 수 있어요. 저장 범위를 좁히거나 필요한 줄만 선택해 주세요. 많은 양은 엑셀·PDF 저장을 써 주세요.');
      return;
    }
    final png = await DispatchExports.capturePng(rows, scopeLabel);
    if (copy) {
      if (await Ui.copyImage(png)) Ui.toast('결과 이미지 ${rows.length}대를 복사했어요. Slack에 붙여넣으세요.', title: '복사했어요');
    } else if (await Ui.save(png, '탁송조회_${stamp('yyyyMMdd_HHmmss')}.png', 'PNG 이미지', ['png']) != null) {
      Ui.toast('결과 이미지를 저장했어요.');
    }
  }

  Future<void> export(String type) async {
    final rows = scopeRows;
    if (!await _guardExport(rows)) return;
    update(() {
      busy = true;
      status = '${type.toUpperCase()} 파일 만드는 중…';
    });
    try {
      final name = '탁송조회_${stamp('yyyyMMdd_HHmmss')}.$type';
      final data = switch (type) {
        'txt' => Uint8List.fromList(utf8.encode(rules.textExport(rows))),
        'xlsx' => await rules.excelExport(rows),
        _ => await DispatchExports.pdfBytes(rows),
      };
      final label = {'txt': '텍스트', 'xlsx': 'Excel 통합 문서', 'pdf': 'PDF'}[type]!;
      final saved = await Ui.save(data, name, label, [type]);
      update(() => status = saved != null ? '${rows.length}대 · ${type.toUpperCase()} 저장 완료' : '파일 저장을 취소했어요.');
      if (saved != null) Ui.toast('결과 파일을 저장했어요.');
    } catch (e) {
      await Ui.alert('파일을 저장하지 못했어요', '$e');
    } finally {
      update(() => busy = false);
    }
  }

  // MARK: 슬랙봇 회신

  bool get hasSlackTargets => results.any((r) => r.flag('cInquiry'));
  bool get hasSlackReplies => results.any((r) => r.flag('slackReviewed'));

  bool applySlack() {
    if (slackText.text.trim().isEmpty) {
      update(() => slackReport = '슬랙봇 회신을 붙여넣어 주세요.');
      return false;
    }
    try {
      final r = rules.applySlackReplies(slackText.text, [for (final x in results) x.raw]);
      update(() {
        results = [for (var i = 0; i < r.rows.length; i++) DRow(i, r.rows[i])];
        slackReport = [
          '${r.applied}대 반영 · 확인 필요 ${r.issues.length}건',
          ...r.issues.take(8),
          if (r.issues.length > 8) '외 ${r.issues.length - 8}건',
        ].join('\n');
      });
      if (r.applied > 0 && r.issues.isEmpty) {
        viewSlackReplies();
        Ui.toast('슬랙봇 회신 ${r.applied}대를 표에 반영했어요.');
        return true;
      }
    } catch (e) {
      update(() => slackReport = '$e');
    }
    return false;
  }

  void viewSlackReplies() => update(() {
        search.clear();
        vendorFilter = 'all';
        stateFilter = 'all';
        scope = 'visible';
        selection = {};
        filter = 'slack-replies';
        page = 0;
      });

  Future<void> reset() async {
    if ((results.isNotEmpty || files.isNotEmpty || input.text.isNotEmpty) &&
        !await Ui.confirm('처음부터 다시 할까요?', '입력한 차량목록, 첨부한 파일, 조회 결과가 지워져요.', ok: '지우기', destructive: true)) {
      return;
    }
    _cleaning = true;
    input.clear();
    _cleaning = false;
    _lastInput = '';
    update(() {
      parsed = null;
      files = [];
      results = [];
      selection = {};
      completed = {};
      copied = {};
      _saveMarks();
      filter = 'all';
      search.clear();
      stale = false;
      slackText.clear();
      slackReport = '';
      page = 0;
      status = '차량목록과 업체 파일을 넣고 조회해 주세요.';
    });
  }
}
