import 'package:fluent_ui/fluent_ui.dart';

import '../ui/theme.dart';

class HomePage extends StatelessWidget {
  const HomePage({super.key, required this.onOpen});
  final ValueChanged<String> onOpen;

  Widget _tool(BuildContext context, IconData icon, Color color, String title, String text, List<String> steps, String button, String key) =>
      Padding(
        padding: const EdgeInsets.symmetric(vertical: 18),
        child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
          IconTile(icon, color, size: 44),
          const SizedBox(width: 16),
          Expanded(
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text(title, style: const TextStyle(fontSize: 17, fontWeight: FontWeight.w600)),
              const SizedBox(height: 4),
              Text(text, style: TextStyle(color: Tone.muted(context))),
              const SizedBox(height: 10),
              for (var i = 0; i < steps.length; i++)
                Padding(
                  padding: const EdgeInsets.only(bottom: 4),
                  child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
                    Container(
                      width: 18, height: 18, alignment: Alignment.center,
                      decoration: BoxDecoration(shape: BoxShape.circle, color: color.withValues(alpha: 0.16)),
                      child: Text('${i + 1}', style: TextStyle(fontSize: 11, fontWeight: FontWeight.w700, color: color)),
                    ),
                    const SizedBox(width: 8),
                    Expanded(child: Text(steps[i])),
                  ]),
                ),
              const SizedBox(height: 10),
              FilledButton(
                style: ButtonStyle(backgroundColor: WidgetStatePropertyAll(color)),
                onPressed: () => onOpen(key),
                child: Text(button),
              ),
            ]),
          ),
        ]),
      );

  @override
  Widget build(BuildContext context) {
    final div = Container(height: 1, color: FluentTheme.of(context).resources.dividerStrokeColorDefault);
    return ScaffoldPage.scrollable(
      padding: const EdgeInsets.symmetric(horizontal: 36, vertical: 28),
      children: [
        ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 820),
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            ShaderMask(
              shaderCallback: (r) => const LinearGradient(colors: [Tone.green, Tone.teal, Tone.blue]).createShader(r),
              child: const Text('업무 도구', style: TextStyle(fontSize: 28, fontWeight: FontWeight.w700, color: Colors.white)),
            ),
            const SizedBox(height: 4),
            Text('넣은 파일과 내용은 이 PC 안에서만 처리하고 서버로 보내지 않아요.', style: TextStyle(color: Tone.muted(context))),
            const SizedBox(height: 20),
            div,
            _tool(context, FluentIcons.car, Tone.green, '탁송체크', '차량목록과 한빛탁송·오픈마일·렌톡 엑셀을 대조해 완료 차량의 금액, 진행 중인 차량, 제주 출발 차량을 정리해요.',
                ['차량목록을 붙여넣어요.', '업체 .xlsx 파일을 끌어다 놓아요. 암호 파일도 돼요.', '차량 조회하기(Ctrl+Enter)를 누르고 결과를 캡처해 Slack에 붙여넣거나 TXT·엑셀·PDF로 받아요.'],
                '탁송체크 열기', 'dispatch:all'),
            div,
            _tool(context, FluentIcons.bank, Tone.orange, '하나은행 이체파일', 'Slack 스레드를 붙여넣으면 은행·계좌번호·금액·차량번호를 뽑아 하나은행 다계좌이체 엑셀로 만들어요.',
                ['이체 요청 입력: 스레드를 붙여넣고 추출해요.', '확인·수정: 은행을 고르고 오류와 검토 필요 건을 정리해요.', '다운로드: 미리보기를 확인하고 엑셀을 받아요.'],
                '이체파일 열기', 'transfer:1'),
            div,
            const SizedBox(height: 16),
            Row(children: [
              Icon(FluentIcons.warning, color: Tone.warn(context), size: 16),
              const SizedBox(width: 8),
              const Expanded(child: Text('이체파일은 은행에 올리기 전에 금액과 계좌를 원문과 꼭 대조해 주세요.')),
            ]),
          ]),
        ),
      ],
    );
  }
}
