#!/usr/bin/env python3
"""실제 WorkTool 화면 스크린샷에 번호·강조 테두리와 설명 카드를 덧붙여요.

1) scripts/capture-guide.cjs 로 화면을 찍고 요소 위치를 scripts/guide-boxes.json 에 저장해요.
2) 이 스크립트가 guide/*-annotated.png 를 만들어요.
화면에 없는 기능이나 고객 정보는 그리지 않아요. 예시 값은 모두 가상이에요.
"""
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
GUIDE = ROOT / "guide"
BOXES = json.loads((ROOT / "scripts" / "guide-boxes.json").read_text(encoding="utf-8"))
FONT_DIR = Path("/usr/share/fonts/opentype/noto")
REG, BOLD, KR = FONT_DIR / "NotoSansCJK-Regular.ttc", FONT_DIR / "NotoSansCJK-Bold.ttc", 1  # index 1 = Noto Sans CJK KR

BLUE, BLUE_DARK, INK, MUTED = "#236de8", "#1d4fa8", "#22324a", "#4c5d75"
BG, CARD_LINE, NOTE_BG, NOTE_LINE, NOTE_INK = "#eef3fb", "#c9d8f0", "#fff5db", "#e2c374", "#6a5216"

CONFIGS = [
  {"key": "dispatch", "dest": "dispatch-annotated.png",
   "note": "렌톡 시트는 '요청시각'이 아닌 '출발시각' 기준으로 조회해서 받아 주세요.",
   "cards": [
     ("input", "차량목록 붙여넣기", "'탁송출발 주유비 미입력' 스크립트에서 복사한 차량번호를 붙여넣어요. 날짜·시간이 같이 있어도 돼요."),
     ("files", "업체 파일 첨부", "한빛탁송·오픈마일·렌톡의 '탁송기한' 시트를 끌어다 놓아요. 인식한 업체와 건수가 보여요."),
     ("run", "차량 조회하기", "누르면 차량번호마다 가장 최신 탁송 기록을 찾아 금액을 정리해요."),
     ("stats", "결과 한눈에 보기", "전체·금액 확인·진행 중·제주 출발·캐리어 대수예요. 누르면 그 차량만 보여요."),
     ("tabs", "걸러 보기", "'확인 필요' 탭부터 보세요. 일정·비용이 여러 개면 표에서 직접 골라요."),
     ("export", "캡처·저장", "결과를 이미지로 복사하거나 PNG로 저장해요. TXT·엑셀·PDF 파일로도 받아요."),
   ]},
  {"key": "input", "dest": "transfer-input-annotated.png",
   "note": "차량번호를 못 찾으면 추출 후 차량번호나 입금통장 메모를 넣는 창이 떠요.",
   "cards": [
     ("text", "Slack 스레드 붙여넣기", "이체 요청 스레드를 그대로 붙여넣어요. 링크·인사말은 알아서 건너뛰어요."),
     ("add", "입력칸 추가", "스레드가 여러 개면 칸을 나눠 넣어요. 같은 내용은 두 번 들어가지 않아요."),
     ("extract", "이체정보 추출하기", "은행·계좌번호·금액·차량번호를 뽑아서 2단계로 넘어가요."),
   ]},
  {"key": "review", "dest": "transfer-review-annotated.png",
   "note": "은행·계좌번호·금액은 꼭 Slack 원문과 대조해 주세요. 줄에 마우스를 올리면 원문이 보여요.",
   "cards": [
     ("tiles", "상태 카드", "정상·검토 필요·오류 건수예요. 누르면 그 상태의 줄만 모아 봐요."),
     ("table", "칸 눌러 고치기", "은행은 목록에서 고르고, 계좌·금액은 칸을 눌러 고쳐요. 상태 칸에 이유가 적혀요."),
     ("memo", "통장 메모·CMS코드", "출금·입금통장표시와 CMS코드를 한 번에 넣을 수 있어요."),
     ("next", "다음 단계로", "빨간 오류를 다 고친 뒤 눌러요."),
   ]},
  {"key": "download", "dest": "transfer-download-annotated.png",
   "note": "이체파일을 만드는 건 실제 송금이 아니에요. 은행 업로드 화면에서 한 번 더 확인해 주세요.",
   "cards": [
     ("preview", "미리보기", "이 표가 엑셀에 그대로 들어가요. 총 건수·금액도 확인해요."),
     ("ack", "검토 확인 체크", "검토 필요 건이 있으면 원문과 대조한 뒤 체크해요. 내용을 바꾸면 다시 체크해요."),
     ("download", "엑셀 다운로드", "하나은행 다계좌이체에 올릴 .xlsx 파일을 받아요. 받은 뒤에도 내역은 남아요."),
     ("comments", "송금 코멘트", "은행에서 송금을 마친 뒤 Slack에 남길 문구예요. 줄마다 또는 전체를 복사해요."),
   ]},
]


def font(path, size):
    return ImageFont.truetype(str(path), size, index=KR)


def wrap(draw, text, fnt, limit):
    """단어 단위로 줄을 나누고, 너무 긴 단어만 글자 단위로 나눠요."""
    rows, cur = [], ""
    for word in text.split():
        trial = (cur + " " + word).strip()
        if draw.textlength(trial, font=fnt) <= limit:
            cur = trial
            continue
        if cur:
            rows.append(cur)
        cur = ""
        for ch in word:
            if cur and draw.textlength(cur + ch, font=fnt) > limit:
                rows.append(cur)
                cur = ""
            cur += ch
    if cur:
        rows.append(cur)
    return rows


def badge(draw, center, n, r, fnt):
    x, y = center
    draw.ellipse((x - r - 3, y - r - 3, x + r + 3, y + r + 3), fill="#ffffff")
    draw.ellipse((x - r, y - r, x + r, y + r), fill=BLUE)
    draw.text((x, y - 1), str(n), fill="#ffffff", font=fnt, anchor="mm")


def render(cfg):
    info = BOXES[cfg["key"]]
    shot = Image.open(GUIDE / info["file"]).convert("RGB")
    W, H = shot.size
    assert [W, H] == info["size"], f"스크린샷과 위치 정보가 맞지 않아요: {cfg['key']}"

    pad, gap = 28, 18
    cols = 4 if len(cfg["cards"]) == 4 else 3
    title_f, body_f, note_f, num_f = font(BOLD, 21 if cols == 4 else 22), font(REG, 17), font(BOLD, 17), font(BOLD, 19)
    cards = cfg["cards"]
    card_w = (W - pad * 2 - gap * (cols - 1)) // cols
    probe = ImageDraw.Draw(shot)
    line_h = 27
    card_h = max(66 + len(wrap(probe, body, body_f, card_w - 40)) * line_h + 18 for _, _, body in cards)
    rows = (len(cards) + cols - 1) // cols
    foot = 24 + 52 + 20 + rows * card_h + (rows - 1) * gap + pad

    out = Image.new("RGB", (W, H + foot), BG)
    out.paste(shot, (0, 0))
    over = Image.new("RGBA", out.size, (0, 0, 0, 0))
    od = ImageDraw.Draw(over)

    # 화면 위: 강조 테두리 + 번호 (설명은 아래 카드에)
    for i, (key, _, _) in enumerate(cards, 1):
        x, y, w, h = info["boxes"][key]
        x, y, w, h = max(4, x - 6), max(4, y - 6), min(W - 8, w + 12), h + 12
        if x + w > W - 4:
            w = W - 4 - x
        od.rounded_rectangle((x, y, x + w, y + h), radius=14, fill=(35, 109, 232, 22), outline=(35, 109, 232, 255), width=4)
    out = Image.alpha_composite(out.convert("RGBA"), over).convert("RGB")
    draw = ImageDraw.Draw(out)
    for i, (key, _, _) in enumerate(cards, 1):
        x, y, w, h = info["boxes"][key]
        cx = min(max(x - 4, 22), W - 22)
        cy = min(max(y - 4, 22), H - 22)
        badge(draw, (cx, cy), i, 17, num_f)

    # 화면 아래: 주의 문구 + 설명 카드
    draw.line((0, H, W, H), fill=CARD_LINE, width=2)
    ny = H + 24
    note = "주의 · " + cfg["note"]
    assert len(wrap(draw, note, note_f, W - pad * 2 - 36)) == 1, f"주의 문구가 너무 길어요: {cfg['key']}"
    draw.rounded_rectangle((pad, ny, W - pad, ny + 52), radius=14, fill=NOTE_BG, outline=NOTE_LINE, width=2)
    draw.text((pad + 18, ny + 26), note, fill=NOTE_INK, font=note_f, anchor="lm")

    top = ny + 52 + 20
    for i, (_, title, body) in enumerate(cards):
        r, c = divmod(i, cols)
        left, ty = pad + c * (card_w + gap), top + r * (card_h + gap)
        draw.rounded_rectangle((left, ty, left + card_w, ty + card_h), radius=16, fill="#ffffff", outline=CARD_LINE, width=2)
        badge(draw, (left + 34, ty + 34), i + 1, 16, num_f)
        draw.text((left + 62, ty + 34), title, fill=INK, font=title_f, anchor="lm")
        for k, line in enumerate(wrap(draw, body, body_f, card_w - 40)):
            draw.text((left + 20, ty + 66 + k * line_h), line, fill=MUTED, font=body_f)

    dest = GUIDE / cfg["dest"]
    out.save(dest, optimize=True)
    print(f"{dest.name}: {out.size[0]}×{out.size[1]}, {dest.stat().st_size:,} bytes")


if __name__ == "__main__":
    for cfg in CONFIGS:
        render(cfg)
