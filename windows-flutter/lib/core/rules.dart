import 'dart:async';
import 'dart:isolate';
import 'dart:typed_data';

import 'package:flutter/services.dart' show rootBundle;

import 'engine.dart';

typedef Json = Map<String, dynamic>;

/// 웹 WorkTool과 같은 규칙(탁송체크·이체파일)을 부르는 창구예요.
/// 가벼운 계산은 화면 쪽 엔진에서 바로, 엑셀 읽기·쓰기처럼 무거운 일은 뒤쪽 작업 엔진에서 해요.
class Rules {
  Rules._(this.e, this._worker);

  static late Rules shared;
  final JsEngine e;
  final _Worker _worker;
  static const transferMax = 100;

  static const dispatchFilters = ['all', 'review', 'confirmed', 'progress', 'missing', 'jeju', 'carrier', 'slack', 'slack-replies'];

  static Future<Rules> load() async {
    final excel = _patchExcel(await rootBundle.loadString('assets/js/excel.js'));
    final dispatch = await rootBundle.loadString('assets/js/dispatch.js');
    final transfer = await rootBundle.loadString('assets/js/transfer.js');
    final engine = JsEngine([('dispatch.js', dispatch), ('transfer.js', transfer)]);
    final worker = await _Worker.start([('excel.js', excel), ('dispatch.js', dispatch), ('transfer.js', transfer)]);
    return shared = Rules._(engine, worker);
  }

  /// 엑셀 라이브러리를 엔진에 맞게 조금 고쳐 불러와요.
  /// 1) 암호 풀 때 SHA-512·SHA-1 10만 번 반복을 C 함수(__nativeSpin)로 해요. 결과는 같고 훨씬 빨라요.
  /// 2) u 플래그 정규식 문자열 안의 `\-`를 같은 뜻인 `\x2d`로 바꿔요 (QuickJS 계열이 `\-`를 거부할 때가 있어요).
  static String _patchExcel(String js) {
    js = js.replaceAll(r'\\-', r'\\x2d');
    const loop = '''        for (let i = 0; i < spinCount; i++) {
            const iterator = this._createUInt32LEBuffer(i);
            key = this._hash(hashAlgorithm, iterator, key);
        }''';
    if (!js.contains(loop)) return js;
    return js.replaceFirst(loop, '''        if (typeof __nativeSpin === "function" && /^sha-?(1|512)\$/i.test(String(hashAlgorithm))) key = Buffer.from(__nativeSpin(new Uint8Array(key), spinCount, /^sha-?1\$/i.test(String(hashAlgorithm))));
        else $loop''');
  }

  // MARK: 탁송체크

  ParsedInput parseInput(String text) {
    e.set('__in', text);
    final r = e.json('DispatchCore.parseInput(__in)') as Map? ?? {};
    return ParsedInput(
      vehicles: List<Json>.from((r['vehicles'] as List? ?? []).map((x) => Json.from(x as Map))),
      invalid: List<String>.from(r['invalid'] as List? ?? []),
      duplicates: (r['duplicates'] as num?)?.toInt() ?? 0,
      cleaned: r['cleaned'] as String? ?? '',
    );
  }

  static bool isEncryptedExcel(Uint8List d) {
    const sig = [0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1];
    if (d.length < 8) return false;
    for (var i = 0; i < 8; i++) {
      if (d[i] != sig[i]) return false;
    }
    return true;
  }

  /// 업체 엑셀 한 개를 읽어요. 결과는 {name,records,sheets,ignored,warnings}.
  Future<Json> readVendorFile(Uint8List data, String name, String? password) async {
    try {
      final r = await _worker.call('read', [data, name, password]);
      return Json.from(r as Map);
    } on EngineError {
      if (password != null) throw EngineError('암호가 맞지 않거나 지원하지 않는 암호화 파일이에요. 암호와 파일을 확인해 주세요.');
      rethrow;
    }
  }

  static String get _annotate =>
      "rs=>rs.map(r=>Object.assign(r,{_label:DispatchCore.statusLabel(r)||'',_notes:DispatchCore.notes(r),_progress:DispatchCore.inProgress(r),"
      "_unresolved:DispatchCore.hasUnresolvedChoice(r),_filters:['${dispatchFilters.join("','")}'].filter(f=>DispatchCore.includes(r,f))}))";

  List<Json> _rows(dynamic v) => List<Json>.from((v as List? ?? []).map((x) => Json.from(x as Map)));

  List<Json> match(List<Json> vehicles, List<Json> files) {
    e.set('__v', vehicles);
    e.set('__f', files);
    return _rows(e.json('($_annotate)(DispatchCore.match(__v,__f))'));
  }

  /// 중첩된 탁송일정·탁송비용 중 하나를 골라요. field는 "schedule" 또는 "fee".
  Json useCandidate(Json row, String field, String id) {
    e.set('__row', row);
    e.set('__field', field);
    e.set('__id', id);
    return Json.from(e.json('(DispatchCore.useCandidate(__row,__field,__id),($_annotate)([__row])[0])') as Map);
  }

  ({List<Json> rows, int applied, List<String> issues}) applySlackReplies(String text, List<Json> rows) {
    e.set('__rows', rows);
    e.set('__text', text);
    final r = e.json('(()=>{const x=DispatchCore.applySlackReplies(__text,__rows);return {rows:($_annotate)(__rows),...x}})()') as Map;
    return (rows: _rows(r['rows']), applied: (r['applied'] as num?)?.toInt() ?? 0, issues: List<String>.from(r['issues'] as List? ?? []));
  }

  List<Json> unique(List<Json> rows) {
    final order = <String>[];
    final last = <String, Json>{};
    for (final r in rows) {
      final p = r['plate'] as String? ?? '';
      if (!last.containsKey(p)) order.add(p);
      last[p] = r;
    }
    return [for (final p in order) last[p]!];
  }

  String textExport(List<Json> rows) {
    e.set('__rows', rows);
    return e.string('DispatchCore.textExport(__rows)');
  }

  Future<Uint8List> excelExport(List<Json> rows) async => await _worker.call('excel', [rows]) as Uint8List;

  // MARK: 이체파일

  late final List<(String, List<String>)> bankGroups = [
    for (final g in e.json('Transfer.banks') as List) (g[0] as String, List<String>.from(g[1] as List)),
  ];
  late final Map<String, String> bankCodes = Map<String, String>.from(e.json('Transfer.codes') as Map);

  dynamic transferCall(String expression, List<Json> rows, [Map<String, Object?> extra = const {}]) {
    e.set('__rows', rows);
    for (final x in extra.entries) {
      e.set(x.key, x.value);
    }
    return e.json(expression);
  }

  Future<Uint8List> transferXlsx(List<Json> rows) async => await _worker.call('transfer', [rows]) as Uint8List;
}

class ParsedInput {
  ParsedInput({required this.vehicles, required this.invalid, required this.duplicates, required this.cleaned});
  final List<Json> vehicles;
  final List<String> invalid;
  final int duplicates;
  final String cleaned;
}

/// 뒤쪽 작업 엔진(별도 isolate). 화면이 멈추지 않게 무거운 일을 여기서 해요.
class _Worker {
  _Worker(this._send, this._replies);
  final SendPort _send;
  final Stream<dynamic> _replies;
  var _next = 0;

  static Future<_Worker> start(List<(String, String)> scripts) async {
    final port = ReceivePort();
    await Isolate.spawn(_main, [port.sendPort, [for (final (n, s) in scripts) [n, s]]], debugName: 'rules');
    final replies = port.asBroadcastStream();
    final first = await replies.first;
    if (first is String) throw EngineError(first);
    return _Worker(first as SendPort, replies);
  }

  Future<dynamic> call(String op, List<Object?> args) async {
    final id = _next++;
    final reply = _replies.firstWhere((m) => m is List && m[0] == id);
    _send.send([id, op, args]);
    final r = await reply as List;
    if (r[1] == true) return r[2];
    throw EngineError(r[2] as String);
  }

  static void _main(List<dynamic> init) {
    final out = init[0] as SendPort;
    final JsEngine e;
    try {
      e = JsEngine([for (final s in init[1] as List) (s[0] as String, s[1] as String)]);
    } catch (err) {
      out.send('규칙을 불러오지 못했어요: $err');
      return;
    }
    final inbox = ReceivePort();
    out.send(inbox.sendPort);
    inbox.listen((m) {
      final id = m[0] as int, op = m[1] as String, args = m[2] as List;
      try {
        out.send([id, true, _run(e, op, args)]);
      } catch (err) {
        e.eval('globalThis.__pw=null;globalThis.__file=null;');
        out.send([id, false, err.toString()]);
      }
    });
  }

  static Object? _run(JsEngine e, String op, List args) {
    switch (op) {
      case 'read':
        e.setBytes('__file', args[0] as Uint8List);
        e.set('__name', args[1]);
        e.set('__pw', args[2]);
        e.wait('(async()=>{const b=__pw!=null?await DispatchExcel.decrypt(__file.buffer,__pw):__file.buffer;'
            'const sheets=await DispatchExcel.readAsync(b);globalThis.__read=DispatchCore.readSheets(sheets,__name);})()');
        e.eval('globalThis.__pw=null;globalThis.__file=null;');
        return e.json('__read');
      case 'excel':
        e.set('__rows', args[0]);
        e.wait('DispatchExport.excelBlob(__rows)');
        return e.bytes('__result');
      case 'transfer':
        e.set('__rows', args[0]);
        return e.bytes('Transfer.xlsx(__rows)');
    }
    throw EngineError('알 수 없는 작업: $op');
  }
}
