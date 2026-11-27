import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart' show rootBundle;

import '../ui/theme.dart';

class HelpPage extends StatelessWidget {
  const HelpPage({super.key});

  Widget _section(BuildContext context, IconData icon, Color color, String title, List<String> lines) => Padding(
        padding: const EdgeInsets.only(bottom: 22),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          SectionTitle(title, icon, color),
          for (final l in lines)
            Padding(
              padding: const EdgeInsets.only(left: 28, bottom: 6),
              child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
                Padding(padding: const EdgeInsets.only(top: 7), child: Container(width: 5, height: 5, decoration: BoxDecoration(color: color, shape: BoxShape.circle))),
                const SizedBox(width: 10),
                Expanded(child: Text(l, style: const TextStyle(height: 1.45))),
              ]),
            ),
        ]),
      );

  @override
  Widget build(BuildContext context) => ScaffoldPage.scrollable(
        header: const PageHeader(title: Text('사용 방법')),
        children: [
          ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 860),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              _section(context, FluentIcons.car, Tone.green, '탁송체크 순서', [
                '사내 ‘탁송출발 주유비 미입력’ 스크립트를 실행하고, 결과에서 차량번호를 추출·복사해요.',
                '오른쪽 패널의 차량목록에 붙여넣어요. 날짜·시간(YYYY-MM-DD HH:MM)을 같이 넣으면 그 값을 탁송일정으로 써요. 빈 줄은 알아서 지워요.',
                '한빛탁송·오픈마일·렌톡에서 ‘탁송기한’ 조회 → 기간 설정 → 조회 → 시트 다운로드를 해요. 렌톡은 출발시각 기준으로 조회해요.',
                '받은 3사 엑셀(.xlsx)을 업체 파일 칸에 끌어다 놓거나 눌러서 골라요(Ctrl+O). 암호 파일은 암호를 물어보고, 암호를 치고 Enter를 누르면 바로 첨부돼요. 암호는 저장하지 않아요.',
                '차량 조회하기(Ctrl+Enter)를 누르고 왼쪽 메뉴의 ‘확인 필요’부터 살펴봐요. 결과 캡처 복사로 Slack에 붙여넣거나 차량번호 복사·TXT·엑셀·PDF로 저장해요.',
              ]),
              _section(context, FluentIcons.lightbulb, Tone.amber, '탁송체크 규칙', [
                '같은 차량의 기록이 여러 개면 업체와 상관없이 탁송일자가 가장 최신인 기록 하나를 써요. 그 기록이 탁송완료일 때만 금액을 확정해요.',
                '주유비용/충전비용: 한빛탁송 ‘주유/부대비용 총액’(빈칸은 0원), 오픈마일 ‘주유비’(빈칸은 미입력). 10원 단위로 반올림해요.',
                '렌톡은 금액 없이 슬랙봇 문의 목록으로 보여요. J열 Y는 캐리어탁송(문의 제외), N은 일반탁송, 빈칸은 확인 필요예요.',
                '같은 차량의 기록끼리 탁송일정·탁송비용이 다르면 표의 ‘일정 선택’·‘비용 선택’에서 직접 골라요. 고르지 않으면 저장하지 않아요.',
                '줄을 체크하면 복사·저장·캡처가 체크한 줄만 대상으로 해요. 일정·차량번호·금액은 눌러서 복사할 수 있어요. 복사하면 알림에 복사한 값이 그대로 나와요.',
                '슬랙봇 회신 반영: ‘차량번호 - 값’을 줄마다 붙여넣어요. X는 0원, ‘확인 중’은 텍스트로 기록해요.',
              ]),
              _section(context, FluentIcons.bank, Tone.orange, '이체파일', [
                'Slack 스레드를 입력칸에 붙여넣어요. 스레드가 여러 개면 ‘입력칸 추가’로 나눠 넣어요.',
                '이체정보 추출하기(Ctrl+Enter)를 누르면 2단계로 넘어가요.',
                '은행·계좌번호·금액·차량번호를 원문과 대조하고, 빨간 칸(오류)을 고쳐요. 줄을 고르면 오른쪽에 원문이 보여요. 필요하면 통장 메모와 CMS코드를 넣어요.',
                '3단계에서 미리보기를 확인하고 ‘이체파일 다운로드’를 눌러요. 송금 코멘트도 복사할 수 있어요.',
                '1억을 넘는 금액은 1억씩 나눠 여러 줄로 만들어요(‘분할’ 표시).',
                '오늘 이미 받은 이체파일과 같은 은행·계좌·금액이 있으면 한 번 더 물어봐요. 계좌번호 원문은 남기지 않아요.',
                '입력 내용은 앱을 닫으면 지워져요. 받지 않은 내역이 있으면 종료 전에 물어봐요. 이 앱은 송금을 실행하지 않아요.',
              ]),
            ]),
          ),
        ],
      );
}

class ChangelogPage extends StatefulWidget {
  const ChangelogPage({super.key});
  @override
  State<ChangelogPage> createState() => _ChangelogPageState();
}

class _ChangelogPageState extends State<ChangelogPage> {
  String text = '';
  String source = '앱에 들어 있는 변경 이력';

  @override
  void initState() {
    super.initState();
    rootBundle.loadString('assets/CHANGELOG.md').then((t) => mounted ? setState(() => text = t) : null);
    _latest();
  }

  Future<void> _latest() async {
    try {
      final c = HttpClient()..connectionTimeout = const Duration(seconds: 6);
      final req = await c.getUrl(Uri.parse('https://raw.githubusercontent.com/chowonki/WorkTool/main/CHANGELOG.md'));
      final res = await req.close();
      if (res.statusCode != 200) return;
      final body = await res.transform(const SystemEncoding().decoder).join();
      c.close();
      if (mounted && body.contains('## ')) setState(() {
        text = body;
        source = 'GitHub 최신';
      });
    } catch (_) {}
  }

  @override
  Widget build(BuildContext context) {
    final blocks = <Widget>[];
    for (final raw in text.split('\n')) {
      final l = raw.trimRight();
      if (l.startsWith('# ') || l.startsWith('[') || l.isEmpty) continue;
      if (l.startsWith('## ')) {
        blocks.add(Padding(
          padding: const EdgeInsets.only(top: 18, bottom: 6),
          child: Text(l.substring(3), style: const TextStyle(fontSize: 15.5, fontWeight: FontWeight.w600)),
        ));
      } else if (l.startsWith('- ')) {
        blocks.add(Padding(
          padding: const EdgeInsets.only(left: 4, bottom: 5),
          child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Padding(padding: const EdgeInsets.only(top: 7), child: Container(width: 5, height: 5, decoration: const BoxDecoration(color: Tone.teal, shape: BoxShape.circle))),
            const SizedBox(width: 10),
            Expanded(child: Text(l.substring(2).replaceAll('**', ''), style: const TextStyle(height: 1.45))),
          ]),
        ));
      } else {
        blocks.add(Text(l.replaceAll('**', ''), style: TextStyle(color: Tone.muted(context))));
      }
    }
    return ScaffoldPage.scrollable(
      header: PageHeader(title: const Text('변경 이력'), commandBar: Text(source, style: TextStyle(color: Tone.muted(context), fontSize: 12.5))),
      children: [ConstrainedBox(constraints: const BoxConstraints(maxWidth: 860), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: blocks))],
    );
  }
}

class SettingsPage extends StatelessWidget {
  const SettingsPage({super.key, required this.mode, required this.onMode, required this.version});
  final ThemeMode mode;
  final ValueChanged<ThemeMode> onMode;
  final String version;

  @override
  Widget build(BuildContext context) => ScaffoldPage.scrollable(
        header: const PageHeader(title: Text('설정')),
        children: [
          SectionTitle('화면 모드', FluentIcons.color, Tone.purple),
          Padding(
            padding: const EdgeInsets.only(left: 28),
            child: Align(
              alignment: Alignment.centerLeft,
              child: ComboBox<ThemeMode>(
                value: mode,
                items: const [
                  ComboBoxItem(value: ThemeMode.system, child: Text('시스템 설정 따르기')),
                  ComboBoxItem(value: ThemeMode.light, child: Text('라이트')),
                  ComboBoxItem(value: ThemeMode.dark, child: Text('다크')),
                ],
                onChanged: (v) => v == null ? null : onMode(v),
              ),
            ),
          ),
          const SizedBox(height: 18),
          SectionTitle('정보', FluentIcons.info, Tone.blue),
          Padding(
            padding: const EdgeInsets.only(left: 28),
            child: Text('WorkTool for Windows $version (Flutter)\n웹과 같은 규칙을 이 PC 안에서 실행해요. 입력과 파일은 서버로 보내지 않아요.\n글꼴: Pretendard (SIL OFL 1.1)',
                style: TextStyle(color: Tone.muted(context), height: 1.5)),
          ),
        ],
      );
}
