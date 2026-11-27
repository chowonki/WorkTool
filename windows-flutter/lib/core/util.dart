import 'dart:convert';
import 'dart:io';

import 'package:path_provider/path_provider.dart';

String two(int n) => n.toString().padLeft(2, '0');

/// 날짜 도장. 형식 글자: yyyy MM dd HH mm ss, M d (앞 0 없음)
String stamp(String format, [DateTime? at]) {
  final d = at ?? DateTime.now();
  return format
      .replaceAll('yyyy', '${d.year}')
      .replaceAll('MM', two(d.month))
      .replaceAll('dd', two(d.day))
      .replaceAll('HH', two(d.hour))
      .replaceAll('mm', two(d.minute))
      .replaceAll('ss', two(d.second))
      .replaceAll(RegExp(r'\bM\b'), '${d.month}')
      .replaceAll(RegExp(r'\bd\b'), '${d.day}');
}

/// 1234567 → "1,234,567"
String won(num v) {
  final neg = v < 0;
  final s = v.abs().round().toString();
  final b = StringBuffer();
  for (var i = 0; i < s.length; i++) {
    if (i > 0 && (s.length - i) % 3 == 0) b.write(',');
    b.write(s[i]);
  }
  return neg ? '-$b' : b.toString();
}

String numText(Object? v) => v is num ? won(v) : '—';

const vendorNames = {'A': '한빛탁송', 'B': '오픈마일', 'C': '렌톡'};
String vendorName(String v) => vendorNames[v] ?? v;

/// 빈 줄(공백만 있는 줄 포함)을 지워요. 마지막 줄바꿈 하나는 남겨서 이어 입력할 수 있게 해요.
String dropBlankLines(String text) {
  final lines = text.replaceAll('\r\n', '\n').replaceAll('\r', '\n').split('\n').where((l) => l.trim().isNotEmpty).toList();
  final joined = lines.join('\n');
  return text.endsWith('\n') && joined.isNotEmpty ? '$joined\n' : joined;
}

/// 복사한 값을 그대로 보여줄 글. 길면 앞 5줄 + "외 n줄"
String copyPreview(String text) {
  final lines = text.replaceAll('\r\n', '\n').split('\n').map((l) => l.trim()).where((l) => l.isNotEmpty).toList();
  if (lines.isEmpty) return '복사했어요.';
  final shown = lines.take(5).map((l) => l.length > 80 ? '${l.substring(0, 80)}…' : l).join('\n');
  return lines.length > 5 ? '$shown\n외 ${lines.length - 5}줄 (총 ${lines.length}줄 복사)' : shown;
}

/// 작은 설정·오늘 기록 저장소(JSON 파일). 포터블이면 실행 폴더의 Data 안에 둬요.
class Store {
  Store._(this._file, this._data);
  static late Store shared;
  final File _file;
  final Map<String, dynamic> _data;

  static Future<Store> load() async {
    Directory dir;
    final exeDir = File(Platform.resolvedExecutable).parent;
    final portable = Directory('${exeDir.path}${Platform.pathSeparator}Data');
    if (Platform.isWindows && (File('${exeDir.path}${Platform.pathSeparator}portable.mode').existsSync() || portable.existsSync())) {
      dir = portable;
    } else {
      dir = await getApplicationSupportDirectory();
    }
    try {
      dir.createSync(recursive: true);
    } catch (_) {
      dir = await getApplicationSupportDirectory();
    }
    final file = File('${dir.path}${Platform.pathSeparator}worktool-state.json');
    Map<String, dynamic> data = {};
    try {
      if (file.existsSync()) data = Map<String, dynamic>.from(jsonDecode(file.readAsStringSync()) as Map);
    } catch (_) {}
    return shared = Store._(file, data);
  }

  dynamic operator [](String key) => _data[key];
  void operator []=(String key, Object? value) {
    if (value == null) {
      _data.remove(key);
    } else {
      _data[key] = value;
    }
    try {
      _file.writeAsStringSync(jsonEncode(_data));
    } catch (_) {}
  }
}
