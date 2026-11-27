import 'dart:io';
import 'dart:ui' as ui;
import 'dart:ui' show AppExitResponse;

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_acrylic/flutter_acrylic.dart' as acrylic;

import 'core/rules.dart';
import 'core/smoke.dart';
import 'core/util.dart';
import 'models/dispatch_model.dart';
import 'models/transfer_model.dart';
import 'pages/dispatch_page.dart';
import 'pages/help_page.dart';
import 'pages/home_page.dart';
import 'pages/transfer_page.dart';
import 'ui/theme.dart';
import 'ui/ui.dart';

const appVersion = '0.4.0';

Future<void> main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();
  if (args.length >= 3 && args[0] == '--smoke-test') {
    exit(await runSmokeTest(args[1], args[2]));
  }
  Object? failure;
  // 시작 중 어떤 오류가 나도 창은 띄우고 이유를 보여 줘요 (오류 기록: startup-error.txt).
  try {
    await Store.load();
  } catch (e, st) {
    failure = e;
    _log('Store.load', e, st);
  }
  if (Platform.isWindows) {
    try {
      await acrylic.Window.initialize();
    } catch (e, st) {
      _log('Window.initialize', e, st);
    }
  }
  try {
    await Rules.load();
  } catch (e, st) {
    failure ??= e;
    _log('Rules.load', e, st);
  }
  try {
    final demo = args.indexOf('--demo');
    if (demo >= 0 && demo + 1 < args.length) Shell.demo = (args[demo + 1], demo + 2 < args.length ? args[demo + 2] : 'dispatch');
    final shot = args.indexOf('--shot');
    if (shot >= 0 && shot + 1 < args.length) WorkToolApp.shot = args[shot + 1];
    if (args.contains('--dark')) Store.shared['appearance'] = 'dark';
    if (args.contains('--light')) Store.shared['appearance'] = 'light';
  } catch (_) {}
  FlutterError.onError = (d) {
    FlutterError.presentError(d);
    _log('FlutterError', d.exception, d.stack);
  };
  runApp(WorkToolApp(failure: failure));
}

/// 시작 오류를 맨 위 폴더(시작 프로그램 옆, 안 되면 임시 폴더)의 startup-error.txt에 남겨요.
void _log(String where, Object e, StackTrace? st) {
  final text = '[${DateTime.now()}] $where: $e\n$st\n';
  for (final dir in [AppPaths.rootDir.path, Directory.systemTemp.path]) {
    try {
      File('$dir${Platform.pathSeparator}startup-error.txt').writeAsStringSync(text, mode: FileMode.append);
      return;
    } catch (_) {}
  }
}

class WorkToolApp extends StatefulWidget {
  const WorkToolApp({super.key, this.failure});
  static String? shot;
  static final boundary = GlobalKey();
  final Object? failure;
  @override
  State<WorkToolApp> createState() => _WorkToolAppState();
}

class _WorkToolAppState extends State<WorkToolApp> {
  /// 화면 캡처(--shot) 때는 Mica 대신 불투명 배경을 써요.
  bool get _mica => Platform.isWindows && WorkToolApp.shot == null;
  ThemeMode mode = switch (Store.shared['appearance']) { 'light' => ThemeMode.light, 'dark' => ThemeMode.dark, _ => ThemeMode.system };

  void setMode(ThemeMode m) {
    setState(() => mode = m);
    Store.shared['appearance'] = m == ThemeMode.light ? 'light' : m == ThemeMode.dark ? 'dark' : 'system';
  }

  FluentThemeData theme(Brightness b) => FluentThemeData(
        brightness: b,
        accentColor: Tone.accent,
        fontFamily: 'Pretendard',
        visualDensity: VisualDensity.standard,
        scaffoldBackgroundColor: _mica ? Colors.transparent : null,
        navigationPaneTheme: NavigationPaneThemeData(backgroundColor: _mica ? Colors.transparent : null),
      );

  @override
  Widget build(BuildContext context) => FluentApp(
        title: 'WorkTool',
        debugShowCheckedModeBanner: false,
        navigatorKey: Ui.navigatorKey,
        themeMode: mode,
        theme: theme(Brightness.light),
        darkTheme: theme(Brightness.dark),
        builder: (context, child) => RepaintBoundary(key: WorkToolApp.boundary, child: _Mica(child: child!)),
        home: widget.failure != null
            ? ScaffoldPage(content: Center(child: Text('규칙을 불러오지 못했어요.\n${widget.failure}', textAlign: TextAlign.center)))
            : Shell(mode: mode, onMode: setMode),
      );
}

/// Windows 11 Mica 배경 (밝기에 맞춰요)
class _Mica extends StatefulWidget {
  const _Mica({required this.child});
  final Widget child;
  @override
  State<_Mica> createState() => _MicaState();
}

class _MicaState extends State<_Mica> {
  Brightness? applied;
  @override
  Widget build(BuildContext context) {
    final b = FluentTheme.of(context).brightness;
    if (Platform.isWindows && WorkToolApp.shot == null && b != applied) {
      applied = b;
      acrylic.Window.setEffect(effect: acrylic.WindowEffect.mica, dark: b == Brightness.dark).catchError((_) {});
    }
    return widget.child;
  }
}

class Shell extends StatefulWidget {
  const Shell({super.key, required this.mode, required this.onMode});
  static (String, String)? demo;
  final ThemeMode mode;
  final ValueChanged<ThemeMode> onMode;
  @override
  State<Shell> createState() => _ShellState();
}

class _ShellState extends State<Shell> {
  Future<void> _runDemo((String, String) d) async {
    final (repo, target) = d;
    final sep = Platform.pathSeparator;
    final dir = '$repo${sep}scripts${sep}fixtures';
    dispatch.input.text = File('$dir$sep차량목록.txt').readAsStringSync();
    await dispatch.addFiles([for (final n in ['한빛탁송_가상.xlsx', '오픈마일_가상.xlsx', '렌톡_가상.xlsx']) '$dir$sep$n']);
    await dispatch.lookup();
    transfer.threads.first.text.text = '현대 쏘나타 123가4567\n국민은행\n123-456-789012\n35,000원\n\n기아 K5 45나6789\n신한은행\n110-123-456789\n1억 2천만원\n\n(압류 계좌)\n우리은행 1002-123-456789 50,000원';
    if (target.startsWith('transfer')) {
      await transfer.extract((a, b) async {});
      if (transfer.rows.isNotEmpty) transfer.focused = transfer.rows.first.id;
      final step = int.tryParse(target.split(':').last) ?? 2;
      if (step != transfer.step) transfer.setStep(step);
    }
    go(target.startsWith('transfer') ? 'transfer' : target.contains(':') ? target : 'dispatch:all');
    final shot = WorkToolApp.shot;
    if (shot != null) {
      await Future.delayed(const Duration(seconds: 2));
      final b = WorkToolApp.boundary.currentContext!.findRenderObject() as RenderRepaintBoundary;
      final img = await b.toImage(pixelRatio: 1.5);
      final png = await img.toByteData(format: ui.ImageByteFormat.png);
      File(shot).writeAsBytesSync(png!.buffer.asUint8List());
      exit(0);
    }
  }

  final dispatch = DispatchModel();
  final transfer = TransferModel();
  String page = 'home';
  late final AppLifecycleListener lifecycle;

  @override
  void initState() {
    super.initState();
    dispatch.addListener(_refresh);
    transfer.addListener(_refresh);
    lifecycle = AppLifecycleListener(onExitRequested: _exitRequested);
    final d = Shell.demo;
    if (d != null) WidgetsBinding.instance.addPostFrameCallback((_) => _runDemo(d));
  }

  @override
  void dispose() {
    lifecycle.dispose();
    super.dispose();
  }

  void _refresh() => setState(() {});

  Future<AppExitResponse> _exitRequested() async {
    if (!transfer.hasUnsaved) return AppExitResponse.exit;
    final ok = await Ui.confirm('저장하지 않은 이체 내역이 있어요', '창을 닫으면 입력 내용과 이체 내역 ${transfer.rows.length}건이 지워져요. 닫을까요?', ok: '닫기', destructive: true);
    return ok ? AppExitResponse.exit : AppExitResponse.cancel;
  }

  void go(String key) {
    if (key.startsWith('dispatch:')) {
      dispatch.setFilter(key.substring(9));
      key = 'dispatch';
    } else if (key.startsWith('transfer:')) {
      transfer.setStep(int.parse(key.substring(9)));
      key = 'transfer';
    }
    setState(() => page = key);
  }

  Widget _badge(int n, Color color) => n == 0
      ? const SizedBox.shrink()
      : Container(
          padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 1),
          decoration: BoxDecoration(color: color.withValues(alpha: 0.16), borderRadius: BorderRadius.circular(10)),
          child: Text('$n', style: TextStyle(fontSize: 11.5, fontWeight: FontWeight.w600, color: color)),
        );

  @override
  Widget build(BuildContext context) {
    final keys = <String>[];
    PaneItem item(String key, Widget icon, String title, Widget body, {Widget? badge}) {
      keys.add(key);
      return PaneItem(icon: icon, title: Text(title), body: body, infoBadge: badge);
    }

    final dispatchBody = DispatchPage(model: dispatch);
    final transferBody = TransferPage(model: transfer);
    const filterIcons = {
      'all': (FluentIcons.bulleted_list, Tone.green), 'review': (FluentIcons.warning, Tone.orange), 'confirmed': (FluentIcons.completed, Tone.green),
      'progress': (FluentIcons.clock, Tone.amber), 'missing': (FluentIcons.unknown, Tone.red), 'jeju': (FluentIcons.airplane, Tone.teal),
      'carrier': (FluentIcons.delivery_truck, Tone.indigo), 'slack': (FluentIcons.chat, Tone.blue), 'slack-replies': (FluentIcons.accept, Tone.purple),
    };
    final has = dispatch.results.isNotEmpty;
    final items = <NavigationPaneItem>[
      item('home', const IconTile(FluentIcons.home, Tone.blue), '홈', HomePage(onOpen: go)),
      PaneItemSeparator(),
      () {
        keys.add('dispatch');
        return PaneItemExpander(
          icon: const IconTile(FluentIcons.car, Tone.green),
          title: const Text('탁송체크'),
          body: dispatchBody,
          initiallyExpanded: true,
          infoBadge: has ? _badge(dispatch.distinctCount, Tone.green) : null,
          items: [
            for (final (k, t) in DispatchModel.filterTitles)
              if (k != 'slack-replies' || dispatch.hasSlackReplies)
                item('dispatch:$k', Icon(filterIcons[k]!.$1, size: 15, color: filterIcons[k]!.$2), t, dispatchBody,
                    badge: has ? _badge(dispatch.count(k), filterIcons[k]!.$2) : null),
          ],
        );
      }(),
      () {
        keys.add('transfer');
        final c = transfer.counts;
        return PaneItemExpander(
          icon: const IconTile(FluentIcons.bank, Tone.orange),
          title: const Text('이체파일'),
          body: transferBody,
          initiallyExpanded: true,
          infoBadge: c.all > 0 ? _badge(c.all, Tone.orange) : null,
          items: [
            for (final (n, t) in [(1, '이체정보 입력'), (2, '확인·수정'), (3, '다운로드')])
              item('transfer:$n', _StepDot(n, transfer.step == n), t, transferBody,
                  badge: n == 2 && c.bad > 0 ? _badge(c.bad, Tone.red) : null),
          ],
        );
      }(),
    ];
    final footer = <NavigationPaneItem>[
      item('help', const IconTile(FluentIcons.help, Tone.purple), '사용 방법', const HelpPage()),
      item('changelog', const IconTile(FluentIcons.history, Tone.teal), '변경 이력', const ChangelogPage()),
      item('settings', const IconTile(FluentIcons.settings, Tone.grey), '설정', SettingsPage(mode: widget.mode, onMode: widget.onMode, version: appVersion)),
    ];
    final current = switch (page) {
      'dispatch' => 'dispatch:${dispatch.filter}',
      'transfer' => 'transfer:${transfer.step}',
      _ => page,
    };
    final selected = keys.indexOf(current);
    Ui.host = context;
    return NavigationView(
      pane: NavigationPane(
        selected: selected < 0 ? 0 : selected,
        onChanged: (i) => go(keys[i]),
        displayMode: PaneDisplayMode.auto,
        size: const NavigationPaneSize(openWidth: 250),
        header: const Padding(
          padding: EdgeInsets.only(left: 4),
          child: Text('WorkTool', style: TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
        ),
        items: items,
        footerItems: footer,
      ),
    );
  }
}

class _StepDot extends StatelessWidget {
  const _StepDot(this.n, this.active);
  final int n;
  final bool active;
  @override
  Widget build(BuildContext context) => Container(
        width: 18,
        height: 18,
        alignment: Alignment.center,
        decoration: BoxDecoration(shape: BoxShape.circle, color: active ? Tone.orange : Tone.orange.withValues(alpha: 0.16)),
        child: Text('$n', style: TextStyle(fontSize: 11, fontWeight: FontWeight.w700, color: active ? Colors.white : Tone.orange)),
      );
}
