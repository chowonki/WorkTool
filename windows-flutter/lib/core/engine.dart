import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

class EngineError implements Exception {
  EngineError(this.message);
  final String message;
  @override
  String toString() => message;
}

/// quickjs-ng(native/wtjs.c) 함수들
class _Native {
  _Native(DynamicLibrary lib)
      : create = lib.lookupFunction<Pointer<Void> Function(IntPtr), Pointer<Void> Function(int)>('wt_new'),
        evaluate = lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, IntPtr, Pointer<Utf8>, Pointer<Int32>),
            Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, int, Pointer<Utf8>, Pointer<Int32>)>('wt_eval'),
        pending = lib.lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>('wt_pending'),
        freeStr = lib.lookupFunction<Void Function(Pointer<Utf8>), void Function(Pointer<Utf8>)>('wt_free_str'),
        free = lib.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>('wt_free');

  final Pointer<Void> Function(int) create;
  final Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, int, Pointer<Utf8>, Pointer<Int32>) evaluate;
  final int Function(Pointer<Void>) pending;
  final void Function(Pointer<Utf8>) freeStr;
  final void Function(Pointer<Void>) free;

  static _Native? _shared;
  static _Native get shared => _shared ??= _Native(_open());

  static DynamicLibrary _open() {
    final env = Platform.environment['WORKTOOL_JS_LIB'];
    if (env != null && env.isNotEmpty) return DynamicLibrary.open(env);
    if (Platform.isWindows) return DynamicLibrary.open('worktool_js.dll');
    final exe = File(Platform.resolvedExecutable).parent.path;
    if (Platform.isMacOS) return DynamicLibrary.open('$exe/../Frameworks/libworktool_js.dylib');
    return DynamicLibrary.open('libworktool_js.so');
  }
}

/// 웹과 같은 규칙 JavaScript를 실행하는 엔진이에요 (quickjs-ng).
/// 브라우저·파일·네트워크 기능은 주지 않아요.
class JsEngine {
  JsEngine(List<(String, String)> scripts) {
    // Dart isolate 스레드 스택(1MB 안팎) 안에서 돌도록 JS 스택을 640KB로 제한해요.
    _ptr = _n.create(640 * 1024);
    if (_ptr == nullptr) throw EngineError('규칙 엔진을 만들지 못했어요.');
    for (final (name, source) in scripts) {
      eval(source, name: name);
    }
  }

  final _n = _Native.shared;
  late final Pointer<Void> _ptr;

  /// 스크립트를 실행하고 String(결과)를 돌려줘요. 오류면 EngineError.
  String eval(String source, {String name = 'eval'}) {
    final bytes = utf8.encode(source);
    final code = malloc<Uint8>(bytes.length + 1);
    code.asTypedList(bytes.length + 1)
      ..setAll(0, bytes)
      ..[bytes.length] = 0;
    final file = 'worktool://$name'.toNativeUtf8();
    final err = malloc<Int32>();
    try {
      final out = _n.evaluate(_ptr, code.cast(), bytes.length, file, err);
      final text = out == nullptr ? '' : _decode(out);
      if (out != nullptr) _n.freeStr(out);
      if (err.value != 0) throw EngineError(_clean(text));
      _n.pending(_ptr);
      return text;
    } finally {
      malloc.free(code);
      malloc.free(file);
      malloc.free(err);
    }
  }

  void executePendingJob() => _n.pending(_ptr);

  /// UTF-8 글을 그대로 읽어요. Dart 기본 변환은 맨 앞 BOM을 지워서, TXT 내보내기용 BOM을 지켜요.
  static String _decode(Pointer<Utf8> p) {
    final bytes = p.cast<Uint8>().asTypedList(p.length);
    final text = utf8.decode(bytes, allowMalformed: true);
    final bom = bytes.length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF;
    return bom && !text.startsWith('\uFEFF') ? '\uFEFF$text' : text;
  }

  static String _clean(String s) {
    final first = s.split('\n').first.trim();
    return first.replaceFirst(RegExp(r'^(Error|TypeError|RangeError|ReferenceError|SyntaxError|InternalError):\s*'), '');
  }

  /// 식의 결과를 JSON으로 받아 Dart 값으로 돌려줘요.
  dynamic json(String expression) {
    final s = eval('(()=>{const __jsonOut=($expression);return __jsonOut===undefined?"null":JSON.stringify(__jsonOut)})()');
    return jsonDecode(s);
  }

  String string(String expression) => eval('String($expression)');

  /// 전역 변수에 Dart 값을 넣어요.
  void set(String name, Object? value) {
    eval('globalThis.$name=JSON.parse(${jsonEncode(jsonEncode(value))});');
  }

  void setBytes(String name, Uint8List data) {
    eval('globalThis.$name=__b64dec("${base64Encode(data)}");');
  }

  Uint8List bytes(String expression) => base64Decode(eval('__b64enc(__bytesOf($expression))'));

  /// Promise를 돌려주는 식을 끝까지 기다려요. 결과는 globalThis.__result에 남아요.
  void wait(String expression) {
    eval('globalThis.__done=false;globalThis.__fail=null;globalThis.__result=undefined;'
        'Promise.resolve().then(()=>($expression)).then(v=>{__result=v;__done=true},e=>{__fail=String(e&&e.message||e);__done=true});');
    var idle = 0;
    while (true) {
      executePendingJob();
      final state = eval('__done?(__fail===null?"ok":"fail"):(__drain()?"busy":"idle")');
      if (state == 'ok') return;
      if (state == 'fail') throw EngineError(eval('__fail'));
      if (state == 'idle' && ++idle > 50) throw EngineError('작업이 끝나지 않았어요.');
      if (state == 'busy') idle = 0;
    }
  }

  void dispose() => _n.free(_ptr);
}
