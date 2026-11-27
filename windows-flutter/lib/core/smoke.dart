import 'dart:io';
import 'dart:typed_data';

import 'exports.dart';
import 'rules.dart';

/// 가상 자료로 규칙·엑셀·내보내기를 검증해요 (CI·배포본 확인용).
/// 사용법: WorkTool.exe --smoke-test <결과 폴더> <WorkTool 저장소 폴더>
Future<int> runSmokeTest(String outDir, String repo) async {
  final out = Directory(outDir)..createSync(recursive: true);
  final log = StringBuffer();
  var failures = 0;
  void say(String s) {
    log.writeln(s);
    stdout.writeln(s);
  }

  void expect(bool ok, String what) {
    say(ok ? '  ✓ $what' : '  ✗ $what');
    if (!ok) failures++;
  }

  Future<T> timed<T>(String what, Future<T> Function() f) async {
    final sw = Stopwatch()..start();
    final r = await f();
    say('    ($what ${sw.elapsedMilliseconds}ms)');
    return r;
  }

  bool zip(Uint8List b) => b.length > 2 && b[0] == 0x50 && b[1] == 0x4B;
  final sep = Platform.pathSeparator;
  String path(String rel) => '$repo$sep${rel.replaceAll('/', sep)}';

  try {
    final rules = await timed('규칙 불러오기', Rules.load);
    say('탁송체크');
    final files = <Json>[];
    for (final name in ['한빛탁송_가상.xlsx', '오픈마일_가상.xlsx', '렌톡_가상.xlsx']) {
      final r = await timed(name, () => rules.readVendorFile(File(path('scripts/fixtures/$name')).readAsBytesSync(), name, null));
      final n = (r['records'] as List?)?.length ?? 0;
      expect(n > 0, '$name: 기록 $n건');
      files.add(r);
    }
    final enc = File(path('windows/WorkTool.Tests/Fixtures/encrypted.xlsx'));
    if (enc.existsSync()) {
      final d = enc.readAsBytesSync();
      expect(Rules.isEncryptedExcel(d), '암호 파일 판별');
      final r = await timed('암호 파일', () => rules.readVendorFile(d, 'encrypted.xlsx', 'fixture-password'));
      expect(((r['records'] as List?)?.first as Map?)?['plate'] == '123가4567', '암호 파일 읽기');
      try {
        await rules.readVendorFile(d, 'encrypted.xlsx', 'wrong');
        expect(false, '틀린 암호 거부');
      } catch (e) {
        expect('$e'.contains('암호'), '틀린 암호 거부: $e');
      }
    }
    final parsed = rules.parseInput(File(path('scripts/fixtures/차량목록.txt')).readAsStringSync());
    expect(parsed.vehicles.length == 10 && parsed.invalid.isEmpty, '차량목록 ${parsed.vehicles.length}대 인식');
    var rows = rules.match(parsed.vehicles, files);
    expect(rows.length == 10, '조회 결과 ${rows.length}대');
    for (final r in rows) {
      say('    ${r['plate']} · ${r['_label']} · 일정 ${r['schedule']} · 금액 ${r['amount'] ?? '-'} · 비용 ${r['dispatchFee'] ?? '-'} · ${r['_notes']}');
    }
    final i = rows.indexWhere((r) => r['scheduleConflict'] == true);
    if (i >= 0) {
      final c = (rows[i]['scheduleCandidates'] as List).first as Map;
      rows[i] = rules.useCandidate(rows[i], 'schedule', c['id'] as String);
      expect(rows[i]['scheduleConflict'] == false, '중첩 일정 선택');
    }
    final target = rows.where((r) => r['cInquiry'] == true).firstOrNull;
    if (target != null) {
      final r = rules.applySlackReplies('${target['plate']} - 12,345원\n잘못된줄', rows);
      expect(r.applied == 1 && r.issues.length == 1, '슬랙봇 회신 반영 ${r.applied}대 · 확인 ${r.issues.length}건');
      rows = r.rows;
    }
    final txt = rules.textExport(rows);
    File('${out.path}$sep탁송조회.txt').writeAsStringSync(txt);
    expect(txt.startsWith('\u{feff}탁송일정\t차량번호'), 'TXT 내보내기');
    final xlsx = await timed('엑셀 만들기', () => rules.excelExport(rules.unique(rows)));
    File('${out.path}$sep탁송조회.xlsx').writeAsBytesSync(xlsx);
    expect(zip(xlsx), '엑셀 내보내기 ${xlsx.length}바이트');
    final png = await DispatchExports.capturePng(rules.unique(rows), '전체 조회 결과');
    File('${out.path}$sep탁송캡처.png').writeAsBytesSync(png);
    expect(png.length > 1000, '캡처 PNG ${png.length}바이트');
    final pdf = await DispatchExports.pdfBytes(rules.unique(rows));
    File('${out.path}$sep탁송조회.pdf').writeAsBytesSync(pdf);
    expect(String.fromCharCodes(pdf.take(4)) == '%PDF', 'PDF ${pdf.length}바이트');

    say('이체파일');
    const thread = '현대 쏘나타 123가4567\nhttps://example.com/vehicle/987654321\n국민은행\n123-456-789012\n35,000원\n\n기아 K5 45나6789\n신한은행\n110-123-456789\n1억 2천만원';
    final res = rules.transferCall("Transfer.extractThread(__raw,0,'t1','')", [], {'__raw': thread}) as Map;
    final added = List<Json>.from((res['added'] as List).map((x) => Json.from(x as Map)));
    expect(added.length == 2, '추출 ${added.length}건');
    final split = List<Json>.from((rules.transferCall('Transfer.splitAll(__rows)', added) as List).map((x) => Json.from(x as Map)));
    expect(split.length == 3, '1억 초과 분할 → ${split.length}줄');
    final states = [for (final c in rules.transferCall('Transfer.check(__rows)', split) as List) (c as Map)['state']];
    expect(states.join(',') == 'ok,warn,warn', '검사 결과 $states');
    say('    코멘트: ${rules.transferCall('Transfer.comments(__rows)', split)}');
    final name = rules.transferCall("Transfer.fileName(__rows,'20261127_0930')", split) as String;
    expect(name.startsWith('하나은행_다계좌이체_20261127_0930_123가4567'), '파일명 $name');
    final x = await timed('이체 엑셀', () => rules.transferXlsx(split));
    File('${out.path}$sep$name').writeAsBytesSync(x);
    expect(zip(x), '이체 엑셀 ${x.length}바이트');
    final banks = rules.bankGroups.expand((g) => g.$2).toList();
    expect(banks.contains('국민') && rules.bankCodes['국민'] == '004', '은행 목록 ${banks.length}개');
  } catch (e, st) {
    say('  ✗ 오류: $e\n$st');
    failures++;
  }
  say(failures == 0 ? '모두 통과 · 결과: ${out.path}' : '실패 $failures건');
  File('${out.path}${sep}smoke-result.txt').writeAsStringSync(log.toString());
  return failures == 0 ? 0 : 1;
}
