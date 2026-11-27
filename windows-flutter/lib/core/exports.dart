import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/painting.dart';
import 'package:pdf/pdf.dart' as pdf;
import 'package:pdf/widgets.dart' as pw;

import 'rules.dart';
import 'util.dart';

/// 탁송 결과 캡처(PNG)와 PDF를 웹·Mac과 같은 배치로 그려요.
class DispatchExports {
  static const captureMax = 300;

  static double _sum(List<Json> rows, String key) => rows.fold(0.0, (a, r) => a + ((r[key] as num?)?.toDouble() ?? 0));

  static (Color, Color) tone(Json r) {
    final key = r['_progress'] == true ? 'progress' : (r['status'] as String? ?? '');
    const t = {
      'confirmed': (0xffe6f4ec, 0xff1f7a4d), 'missing': (0xfffdf0e2, 0xffa65a10), 'progress': (0xfffff4dc, 0xff8a5a00),
      'incomplete': (0xfffbe9e9, 0xffa33a3a), 'cost': (0xfffbe9e9, 0xffa33a3a), 'slack': (0xffe6effc, 0xff1d4fa8),
      'carrier': (0xffe6f4ec, 0xff1f7a4d), 'duplicate': (0xfff1f1f4, 0xff555b66),
    };
    final c = t[key] ?? t['duplicate']!;
    return (Color(c.$1), Color(c.$2));
  }

  static TextStyle _font(double size, [FontWeight w = FontWeight.w400, Color color = const Color(0xff2a3a52)]) =>
      TextStyle(fontFamily: 'Pretendard', fontSize: size, fontWeight: w, color: color, height: 1.2);

  /// 글자가 칸보다 길면 …로 줄여서 그려요. midY는 글자 세로 가운데.
  static double _draw(Canvas c, String text, TextStyle style, double x, double midY, double width, {bool right = false}) {
    final tp = TextPainter(text: TextSpan(text: text, style: style), textDirection: TextDirection.ltr, maxLines: 1, ellipsis: '…')
      ..layout(maxWidth: width < 1 ? 1 : width);
    tp.paint(c, Offset(right ? x + width - tp.width : x, midY - tp.height / 2));
    return tp.width;
  }

  static String _sched(Json r) => r['scheduleConflict'] == true && (r['schedule'] as String? ?? '').isEmpty
      ? '선택 필요'
      : ((r['schedule'] as String?)?.isNotEmpty == true ? r['schedule'] as String : '—');

  /// 웹 captureCanvas와 같은 표 이미지(2배 해상도 PNG)
  static Future<Uint8List> capturePng(List<Json> rows, String scopeLabel, [DateTime? at]) async {
    const s = 2.0, pad = 28.0, rowH = 36.0, head = 40.0, top = 92.0;
    const cols = [('탁송일정', 150.0, false), ('차량번호', 118.0, false), ('주유비용/충전비용(원)', 150.0, true), ('탁송비용(원)', 112.0, true),
      ('부가세(원)', 96.0, true), ('조회 상태', 150.0, false), ('업체', 96.0, false)];
    final w = pad * 2 + cols.fold(0.0, (a, c) => a + c.$2), h = top + head + rows.length * rowH + 70;
    final rec = ui.PictureRecorder();
    final c = Canvas(rec)..scale(s);
    c.drawRect(Rect.fromLTWH(0, 0, w, h), Paint()..color = const Color(0xffffffff));
    _draw(c, '탁송 조회 결과', _font(22, FontWeight.w700, const Color(0xff22324a)), pad, 37, w);
    _draw(c, '$scopeLabel · 차량 ${rows.length}대 · ${stamp('yyyy-MM-dd HH:mm', at)}', _font(13, FontWeight.w400, const Color(0xff5b6d85)), pad, 66, w);
    var y = top;
    c.drawRect(Rect.fromLTWH(pad, y, w - pad * 2, head), Paint()..color = const Color(0xffeef3fb));
    var x = pad;
    for (final (t, cw, right) in cols) {
      _draw(c, t, _font(12.5, FontWeight.w700, const Color(0xff3d5170)), x + 12, y + head / 2, cw - 24, right: right);
      x += cw;
    }
    y += head;
    final line = Paint()..color = const Color(0xffe3e9f2)..strokeWidth = 1;
    for (var i = 0; i < rows.length; i++) {
      final r = rows[i];
      if (i.isOdd) c.drawRect(Rect.fromLTWH(pad, y, w - pad * 2, rowH), Paint()..color = const Color(0xfff7f9fc));
      final fee = r['feeConflict'] == true && r['dispatchFee'] == null ? '선택 필요' : numText(r['dispatchFee']);
      final amount = r['slackPending'] == true ? '확인 중' : numText(r['amount']);
      final vendor = (r['vendor'] as String?)?.isNotEmpty == true ? r['vendor'] as String : '—';
      final vals = [_sched(r), r['plate'] as String? ?? '', amount, fee, numText(r['vat']), r['_label'] as String? ?? '', vendor];
      x = pad;
      for (var k = 0; k < vals.length; k++) {
        final (_, cw, right) = cols[k];
        final v = vals[k];
        if (k == 5) {
          final (bg, fg) = tone(r);
          final f = _font(12, FontWeight.w700, fg);
          final tp = TextPainter(text: TextSpan(text: v, style: f), textDirection: TextDirection.ltr, maxLines: 1)..layout();
          final tw = (tp.width < cw - 30 ? tp.width : cw - 30) + 16;
          c.drawRRect(RRect.fromRectAndRadius(Rect.fromLTWH(x + 10, y + 8, tw, rowH - 16), const Radius.circular(9)), Paint()..color = bg);
          _draw(c, v, f, x + 18, y + rowH / 2, cw - 30);
        } else {
          final color = v == '선택 필요' ? const Color(0xffa65a10) : const Color(0xff2a3a52);
          _draw(c, v, k == 1 ? _font(14, FontWeight.w700, color) : _font(13.5, FontWeight.w400, color), x + 12, y + rowH / 2, cw - 24, right: right);
        }
        x += cw;
      }
      c.drawLine(Offset(pad, y + rowH + 0.5), Offset(w - pad, y + rowH + 0.5), line);
      y += rowH;
    }
    _draw(c, '합계 · 주유비용/충전비용 ${won(_sum(rows, 'amount'))}원 · 탁송비용 ${won(_sum(rows, 'dispatchFee'))}원 · 부가세 ${won(_sum(rows, 'vat'))}원',
        _font(13, FontWeight.w700, const Color(0xff22324a)), pad, y + 26, w - pad * 2);
    c.drawRect(Rect.fromLTWH(pad + 0.5, top + 0.5, w - pad * 2 - 1, head + rows.length * rowH - 1),
        Paint()..color = const Color(0xffd6dfeb)..style = PaintingStyle.stroke);
    final img = await rec.endRecording().toImage((w * s).round(), (h * s).round());
    final data = await img.toByteData(format: ui.ImageByteFormat.png);
    img.dispose();
    return data!.buffer.asUint8List();
  }

  /// 웹 pdfBlob과 같은 A4 세로 표(쪽당 19대). 글꼴을 그대로 쓰려고 쪽마다 그림으로 그려 넣어요.
  static Future<Uint8List> pdfBytes(List<Json> rows, [DateTime? at]) async {
    const mm = 8.0; // 1mm = 8px (약 200dpi)
    final pages = rows.isEmpty ? 1 : (rows.length / 19).ceil();
    const xs = [14.0, 47.0, 76.0, 102.0, 130.0, 154.0], widths = [33.0, 29.0, 26.0, 28.0, 24.0, 42.0];
    double pt(double v) => v * 0.3528; // 글자 pt → mm
    final doc = pw.Document(title: '탁송체크 · 조회결과', creator: 'WorkTool for Windows');
    const green = Color(0xff244c3b), grey = Color(0xff828282);
    for (var p = 0; p < pages; p++) {
      final rec = ui.PictureRecorder();
      final c = Canvas(rec)..scale(mm);
      c.drawRect(const Rect.fromLTWH(0, 0, 210, 297), Paint()..color = const Color(0xffffffff));
      _draw(c, '탁송체크 · 조회결과', _font(pt(18), FontWeight.w400, green), 14, 18.5, 180);
      _draw(c, '차량 ${rows.length}대 · ${stamp('yyyy. M. d. HH:mm:ss', at)}', _font(pt(8), FontWeight.w400, grey), 14, 29, 180);
      _draw(c, '완료 기준 / 10원 단위 반올림 / C업체 금액 제외 / 차량번호 중복 제거', _font(pt(8), FontWeight.w400, grey), 14, 35, 180);
      c.drawRect(const Rect.fromLTWH(14, 43, 182, 11), Paint()..color = green);
      const heads = ['탁송일정', '차량번호', '주유·충전(원)', '탁송비(원)', '부가세(원)', '조회 상태'];
      for (var i = 0; i < heads.length; i++) {
        _draw(c, heads[i], _font(pt(8), FontWeight.w400, const Color(0xffffffff)), xs[i] + 2, 48.5, widths[i]);
      }
      final chunk = rows.skip(p * 19).take(19).toList();
      for (var i = 0; i < chunk.length; i++) {
        final r = chunk[i];
        final y = 54 + i * 10.7;
        if (i.isEven) c.drawRect(Rect.fromLTWH(14, y, 182, 10.7), Paint()..color = const Color(0xfff5f8f5));
        final sched = (r['schedule'] as String?)?.isNotEmpty == true ? r['schedule'] as String : '—';
        final vals = [sched, r['plate'] as String? ?? '', r['slackPending'] == true ? '확인 중' : numText(r['amount']), numText(r['dispatchFee']),
          numText(r['vat']), r['_label'] as String? ?? ''];
        for (var k = 0; k < vals.length; k++) {
          _draw(c, vals[k], _font(pt(7), FontWeight.w400, const Color(0xff37453e)), xs[k] + 2, y + 5.35, widths[k] - 5);
        }
      }
      _draw(c, '${p + 1} / $pages', _font(pt(8), FontWeight.w400, const Color(0xff969696)), 150, 285, 46, right: true);
      final img = await rec.endRecording().toImage((210 * mm).round(), (297 * mm).round());
      final png = (await img.toByteData(format: ui.ImageByteFormat.png))!.buffer.asUint8List();
      img.dispose();
      doc.addPage(pw.Page(
        pageFormat: pdf.PdfPageFormat.a4,
        margin: pw.EdgeInsets.zero,
        build: (_) => pw.Image(pw.MemoryImage(png), fit: pw.BoxFit.fill),
      ));
    }
    return doc.save();
  }
}
