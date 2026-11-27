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

/// 실행 파일 위치로 정해지는 폴더들이에요.
///
/// 포터블 배포본은 이렇게 생겼어요:
/// ```
/// WorkTool\
///   WorkTool.exe   시작 프로그램
///   App\           앱 파일 (이 실행 파일, DLL, Flutter 엔진의 data 폴더)
///   Data\          설정·오늘 기록
/// ```
/// 사용자 자료를 App 밖의 Data에 두기 때문에 엔진의 `App\data`와 섞이지 않아요.
class AppPaths {
  AppPaths._();

  static final String _sep = Platform.pathSeparator;

  /// 실행 파일이 있는 폴더.
  static final Directory exeDir = File(Platform.resolvedExecutable).parent;

  /// 실행 파일이 포터블 배포본의 App 폴더 안에 있는지.
  static final bool _inAppFolder = exeDir.path.split(RegExp(r'[\\/]')).last.toLowerCase() == 'app';

  /// 포터블 방식인지 (실행 파일 옆에 `portable.mode`가 있는 Windows 배포본).
  static final bool portable = Platform.isWindows && File('${exeDir.path}${_sep}portable.mode').existsSync();

  /// 사용자가 보는 맨 위 폴더 (시작 프로그램이 있는 곳). App 구조가 아니면 실행 폴더예요.
  static final Directory rootDir = _inAppFolder ? exeDir.parent : exeDir;

  /// 포터블 방식에서 설정을 두는 폴더.
  /// App 구조가 아닌 곳(빌드 폴더에서 바로 실행 등)에서는 엔진의 data와 겹치지 않게 UserData를 써요.
  static final Directory portableDataDir =
      Directory(_inAppFolder ? '${rootDir.path}${_sep}Data' : '${exeDir.path}${_sep}UserData');
}

/// 작은 설정·오늘 기록 저장소(JSON 파일). 포터블이면 [AppPaths.portableDataDir], 아니면 사용자 폴더에 둬요.
class Store {
  Store._(this._file, this._data);
  static late Store shared;
  final File _file;
  final Map<String, dynamic> _data;

  static Future<Store> load() async {
    Directory dir = Directory.systemTemp;
    try {
      if (AppPaths.portable) {
        dir = AppPaths.portableDataDir;
        dir.createSync(recursive: true);
        // 쓰기 되는지 확인 (Program Files처럼 막힌 곳이면 사용자 폴더를 써요)
        File('${dir.path}${Platform.pathSeparator}.write-test').writeAsStringSync('ok');
      } else {
        dir = await getApplicationSupportDirectory();
        dir.createSync(recursive: true);
      }
    } catch (_) {
      try {
        dir = await getApplicationSupportDirectory();
        dir.createSync(recursive: true);
      } catch (_) {
        dir = Directory.systemTemp;
      }
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
