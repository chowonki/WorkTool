import 'package:fluent_ui/fluent_ui.dart';

/// WorkTool 색 (라이트·다크)
class Tone {
  static const green = Color(0xff267358);
  static const greenDark = Color(0xff3a9a76);

  static final accent = AccentColor.swatch(const {
    'darkest': Color(0xff123b2c),
    'darker': Color(0xff185040),
    'dark': Color(0xff1f624b),
    'normal': green,
    'light': Color(0xff3a9a76),
    'lighter': Color(0xff5fb592),
    'lightest': Color(0xff8fd0b2),
  });

  static const blue = Color(0xff2f6fdb);
  static const orange = Color(0xffd9822b);
  static const red = Color(0xffc8433f);
  static const purple = Color(0xff8a5cd6);
  static const teal = Color(0xff1e9aa8);
  static const pink = Color(0xffd6508a);
  static const indigo = Color(0xff4f5bd5);
  static const amber = Color(0xffc79100);
  static const grey = Color(0xff6b7280);

  static Color ok(BuildContext c) => _dark(c) ? const Color(0xff4cc38a) : const Color(0xff1f7a4d);
  static Color warn(BuildContext c) => _dark(c) ? const Color(0xffe8a33d) : const Color(0xffa65a10);
  static Color bad(BuildContext c) => _dark(c) ? const Color(0xffef6b6b) : const Color(0xffb3261e);
  static Color info(BuildContext c) => _dark(c) ? const Color(0xff7aa7ff) : const Color(0xff1d4fa8);
  static Color muted(BuildContext c) => FluentTheme.of(c).resources.textFillColorSecondary;
  static bool _dark(BuildContext c) => FluentTheme.of(c).brightness == Brightness.dark;

  /// 조회 상태 색 (글자색, 배경색)
  static (Color, Color) status(BuildContext c, Map row) {
    final key = row['_progress'] == true ? 'progress' : (row['status'] as String? ?? '');
    final fg = switch (key) {
      'confirmed' || 'carrier' => ok(c),
      'missing' || 'progress' => warn(c),
      'incomplete' || 'cost' => bad(c),
      'slack' => info(c),
      _ => muted(c),
    };
    return (fg, fg.withValues(alpha: _dark(c) ? 0.20 : 0.12));
  }

  static const mono = TextStyle(fontFamily: 'Consolas', fontFamilyFallback: ['Pretendard', 'Menlo'], fontSize: 13);
}

/// 색 아이콘 타일 (설정 앱처럼)
class IconTile extends StatelessWidget {
  const IconTile(this.icon, this.color, {super.key, this.size = 22});
  final IconData icon;
  final Color color;
  final double size;
  @override
  Widget build(BuildContext context) => Container(
        width: size,
        height: size,
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(size * 0.26),
          gradient: LinearGradient(begin: Alignment.topCenter, end: Alignment.bottomCenter, colors: [Color.lerp(color, Colors.white, 0.18)!, color]),
        ),
        alignment: Alignment.center,
        child: Icon(icon, size: size * 0.58, color: Colors.white),
      );
}

/// 색 알약 (상태 표시)
class Pill extends StatelessWidget {
  const Pill(this.text, this.fg, this.bg, {super.key});
  final String text;
  final Color fg, bg;
  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 3),
        decoration: BoxDecoration(color: bg, borderRadius: BorderRadius.circular(20)),
        child: Row(mainAxisSize: MainAxisSize.min, children: [
          Container(width: 6, height: 6, decoration: BoxDecoration(color: fg, shape: BoxShape.circle)),
          const SizedBox(width: 6),
          Flexible(child: Text(text, maxLines: 1, overflow: TextOverflow.ellipsis, style: TextStyle(color: fg, fontSize: 12, fontWeight: FontWeight.w600))),
        ]),
      );
}

/// 구역 제목 (색 아이콘 + 글)
class SectionTitle extends StatelessWidget {
  const SectionTitle(this.title, this.icon, this.color, {super.key, this.trailing});
  final String title;
  final IconData icon;
  final Color color;
  final Widget? trailing;
  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(bottom: 8),
        child: Row(children: [
          IconTile(icon, color, size: 20),
          const SizedBox(width: 8),
          Expanded(child: Text(title, style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600))),
          ?trailing,
        ]),
      );
}

/// 오른쪽 패널 틀
class SidePanel extends StatelessWidget {
  const SidePanel({super.key, required this.children, this.width = 320});
  final List<Widget> children;
  final double width;
  @override
  Widget build(BuildContext context) {
    final r = FluentTheme.of(context).resources;
    return Container(
      width: width,
      decoration: BoxDecoration(
        color: r.layerFillColorDefault,
        border: Border(left: BorderSide(color: r.cardStrokeColorDefault)),
      ),
      child: ListView(padding: const EdgeInsets.fromLTRB(16, 14, 16, 20), children: children),
    );
  }
}
