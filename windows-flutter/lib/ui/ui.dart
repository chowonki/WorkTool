import 'dart:io';
import 'dart:typed_data';

import 'package:file_selector/file_selector.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart';
import 'package:pasteboard/pasteboard.dart';

import '../core/util.dart';

/// 대화상자·알림·저장·클립보드
class Ui {
  static final navigatorKey = GlobalKey<NavigatorState>();
  static BuildContext get _ctx => navigatorKey.currentState!.overlay!.context;

  static void toast(String message, {bool error = false, String? title}) {
    final ctx = navigatorKey.currentState?.overlay?.context;
    if (ctx == null) return;
    displayInfoBar(ctx, duration: Duration(milliseconds: message.contains('\n') ? 4500 : 3000), builder: (context, close) => InfoBar(
          title: Text(title ?? (error ? '확인해 주세요' : '완료')),
          content: ConstrainedBox(constraints: const BoxConstraints(maxWidth: 520), child: Text(message)),
          severity: error ? InfoBarSeverity.warning : InfoBarSeverity.success,
          isLong: message.contains('\n'),
          onClose: close,
        ));
  }

  static Widget _details(String text) => Container(
        margin: const EdgeInsets.only(top: 12),
        constraints: const BoxConstraints(maxHeight: 200),
        decoration: BoxDecoration(
          border: Border.all(color: FluentTheme.of(_ctx).resources.controlStrokeColorDefault),
          borderRadius: BorderRadius.circular(6),
        ),
        child: SingleChildScrollView(padding: const EdgeInsets.all(10), child: SelectableText(text, style: const TextStyle(fontSize: 12.5))),
      );

  static Future<void> alert(String title, String text, {String details = ''}) => showDialog<void>(
        context: _ctx,
        builder: (context) => ContentDialog(
          title: Text(title),
          content: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(text),
            if (details.isNotEmpty) _details(details),
          ]),
          actions: [FilledButton(autofocus: true, onPressed: () => Navigator.pop(context), child: const Text('확인'))],
        ),
      );

  static Future<bool> confirm(String title, String text, {String ok = '확인', bool destructive = false, String details = ''}) async =>
      await showDialog<bool>(
        context: _ctx,
        builder: (context) => ContentDialog(
          title: Text(title),
          content: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(text),
            if (details.isNotEmpty) _details(details),
          ]),
          actions: [
            FilledButton(
              autofocus: !destructive,
              style: destructive ? ButtonStyle(backgroundColor: WidgetStatePropertyAll(Colors.red.dark)) : null,
              onPressed: () => Navigator.pop(context, true),
              child: Text(ok),
            ),
            Button(autofocus: destructive, onPressed: () => Navigator.pop(context, false), child: const Text('취소')),
          ],
        ),
      ) ??
      false;

  /// 암호 입력. 암호 칸에 바로 커서가 가고, Enter를 누르면 열어요. 취소하면 null.
  static Future<String?> password(String title, String fileName) {
    final controller = TextEditingController();
    return showDialog<String>(
      context: _ctx,
      builder: (context) => ContentDialog(
        title: Text(title),
        content: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(fileName, style: const TextStyle(fontWeight: FontWeight.w600)),
          const SizedBox(height: 4),
          const Text('암호는 이 파일을 읽을 때만 사용하며 저장하지 않아요.', style: TextStyle(fontSize: 12.5)),
          const SizedBox(height: 12),
          PasswordBox(
            controller: controller,
            autofocus: true,
            placeholder: '엑셀 암호',
            revealMode: PasswordRevealMode.peek,
            onSubmitted: (v) => Navigator.pop(context, v),
          ),
        ]),
        actions: [
          FilledButton(onPressed: () => Navigator.pop(context, controller.text), child: const Text('불러오기')),
          Button(onPressed: () => Navigator.pop(context), child: const Text('취소')),
        ],
      ),
    ).whenComplete(() => controller.clear());
  }

  static Future<void> copy(String text, {bool preview = true}) async {
    await Clipboard.setData(ClipboardData(text: text));
    if (preview) toast(copyPreview(text), title: '복사했어요');
  }

  static Future<bool> copyImage(Uint8List png) async {
    try {
      await Pasteboard.writeImage(png);
      return true;
    } catch (e) {
      toast('이미지를 클립보드에 넣지 못했어요: $e', error: true);
      return false;
    }
  }

  /// 저장 위치를 묻고 저장해요. 취소하면 null.
  static Future<String?> save(Uint8List data, String name, String label, List<String> extensions) async {
    final loc = await getSaveLocation(
      suggestedName: name,
      acceptedTypeGroups: [XTypeGroup(label: label, extensions: extensions)],
    );
    if (loc == null) return null;
    var path = loc.path;
    if (!extensions.any((e) => path.toLowerCase().endsWith('.$e'))) path = '$path.${extensions.first}';
    try {
      await File(path).writeAsBytes(data, flush: true);
      return path;
    } catch (e) {
      await alert('파일을 저장하지 못했어요', '$e');
      return null;
    }
  }
}
