#!/usr/bin/env python3
"""Add non-obscuring Korean step-by-step overlays to REAL WorkTool screenshots.

The source images were captured from the WorkTool HTML pages with example data.
No invented webpages, features or customer information are drawn.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
GUIDE = ROOT / "guide"
FONT = "/usr/share/fonts/truetype/nanum/NanumGothic.ttf"
BOLD = "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf"

CONFIGS = [
  {
    "source": "dispatch-screen.png",
    "dest": "dispatch-annotated.png",
    "note": "렌톡 시트는 반드시 '요청시각'이 아닌 '출발시각' 기준으로 조회·다운로드",
    "cards": [
      ("차량번호 붙여넣기", "사내 '탁송출발 주유비 미입력' 스크립트에서 차량번호를 추출·복사합니다.", (.15,.61)),
      ("업체 파일 첨부", "한빛탁송·오픈마일·렌톡의 '탁송기한' 조회 시트를 다운로드해 첨부합니다.", (.17,.89)),
      ("조회 결과 확인", "조회 후 주유비 확인 필요, 미조회, 미완료, 중복 검토 항목을 확인합니다.", (.69,.64)),
    ]
  },
  {
    "source": "transfer-screen.png",
    "dest": "transfer-input-annotated.png",
    "note": "차량번호가 없으면 추출 시 차량번호 또는 입금통장 이체 메모 입력 팝업이 열립니다.",
    "cards": [
      ("Slack 원문 입력", "Slack 스레드의 이체 요청 내용을 입력칸에 그대로 붙여넣습니다.", (.46,.71)),
      ("이체정보 추출하기", "입력 후 '이체정보 추출하기' 버튼을 누릅니다.", (.17,.86)),
      ("차량번호 누락 시", "팝업에서 차량번호 또는 이체 메모를 선택합니다. 메모는 입금통장에만 적용됩니다.", (.64,.70)),
    ]
  },
  {
    "source": "transfer-Review.png",
    "dest": "transfer-review-annotated.png",
    "note": "은행명·계좌번호·송금금액은 반드시 Slack 원문과 대조하세요.",
    "cards": [
      ("이체정보 확인", "표에서 은행명, 계좌번호, 송금금액을 각각 확인합니다.", (.26,.81)),
      ("필요한 값 수정", "이체 행을 직접 수정하고 통장 메모·CMS코드는 필요할 때 설정합니다.", (.73,.81)),
      ("검토 필요 항목", "검토가 필요한 항목이 있는지 확인한 후 파일을 다운로드합니다.", (.35,.41)),
    ]
  },
  {
    "source": "transfer-Download.png",
    "dest": "transfer-download-annotated.png",
    "note": "이체파일 생성은 실제 송금 완료를 의미하지 않습니다.",
    "cards": [
      ("Excel 다운로드", "'검토한 내역 Excel 다운로드'를 눌러 업로드용 파일을 저장합니다.", (.14,.54)),
      ("송금 전 확인", "은행 업로드 화면에서 수취인·계좌번호·금액을 다시 확인하세요.", (.53,.34)),
      ("입력 내용 보존", "파일을 다운로드해도 입력 내용이 남습니다. 저장 확인 후 전체 초기화하세요.", (.51,.71)),
    ]
  }
]

def fonts(width):
    scale = min(1.0, width / 1366)
    return (
        ImageFont.truetype(BOLD, max(18, round(23*scale))),
        ImageFont.truetype(FONT, max(15, round(18*scale))),
        ImageFont.truetype(BOLD, max(15, round(17*scale))),
    )

def wrap_pixels(draw, text, font, limit):
    # Split even unspaced Korean text at a character boundary if necessary.
    words=text.split()
    rows=[]
    current=""
    for word in words:
        trial=(current+" "+word).strip()
        if draw.textlength(trial, font=font)<=limit:
            current=trial
            continue
        if current:
            rows.append(current)
        current=""
        for ch in word:
            if draw.textlength(current+ch,font=font)>limit and current:
                rows.append(current)
                current=""
            current+=ch
    if current:
        rows.append(current)
    return rows

def render(config):
    image=Image.open(GUIDE/config["source"]).convert("RGB")
    width,height=image.size
    margin=max(16,round(width*.015))
    gap=max(10,round(width*.012))
    cardw=(width-margin*2-gap*2)//3
    ext=max(296,round(width*.24))
    result=Image.new("RGB",(width,height+ext),"#eff6f2")
    result.paste(image,(0,0))
    draw=ImageDraw.Draw(result)
    titlefont,bodyfont,notefont=fonts(width)
    green="#087b68"
    # Clean white-green foot panel clearly separate from the screenshot.
    draw.rectangle((0,height,width,height+ext),fill="#eaf4ef")
    draw.line((0,height,width,height),fill="#9fcbb9",width=3)
    note_y=height+13
    draw.rounded_rectangle((margin,note_y,width-margin,note_y+48),radius=12,fill="#fff6db",outline="#daba68",width=2)
    note_rows=wrap_pixels(draw,"주의 · "+config["note"],notefont,width-margin*2-28)
    if len(note_rows)>1:
        raise ValueError(f"Note unexpectedly wrapped: {config['dest']}: {note_rows}")
    draw.text((margin+14,note_y+13),note_rows[0],fill="#67511e",font=notefont)
    card_y=height+76
    card_h=ext-92
    if card_h<170:
        raise AssertionError("Insufficient caption height")
    for i,(heading,body,(rx,ry)) in enumerate(config["cards"]):
        left=margin+i*(cardw+gap)
        right=left+cardw
        target=(round(rx*width),round(ry*height))
        anchor=(round((left+right)/2),card_y)
        # Only a small ring + thin connector is placed on top of the real UI.
        draw.line([target,anchor],fill="#ffffff",width=8)
        draw.line([target,anchor],fill=green,width=3)
        draw.ellipse((target[0]-18,target[1]-18,target[0]+18,target[1]+18),fill="#ffffff",outline=green,width=4)
        draw.text((target[0]-5,target[1]-11),str(i+1),fill=green,font=notefont)
        draw.rounded_rectangle((left,card_y,right,card_y+card_h),radius=14,fill="#ffffff",outline="#99cabb",width=3)
        num_x=left+18
        draw.ellipse((num_x,card_y+14,num_x+33,card_y+47),fill=green)
        draw.text((num_x+11,card_y+18),str(i+1),fill="#ffffff",font=notefont)
        draw.text((left+60,card_y+17),heading,font=titlefont,fill="#173e32")
        lines=wrap_pixels(draw,body,bodyfont,cardw-36)
        line_height=round(bodyfont.size*1.48)
        if len(lines)*line_height+65>card_h:
            raise AssertionError(f"Caption overflows card: {config['dest']}: {lines}")
        for k,line in enumerate(lines):
            draw.text((left+18,card_y+65+k*line_height),line,font=bodyfont,fill="#385d50")
    output=GUIDE/config["dest"]
    result.save(output,optimize=True)
    assert output.stat().st_size>30000
    print(f"Saved {output}: {result.size} {output.stat().st_size:,} bytes")

if __name__=="__main__":
    for config in CONFIGS:
        render(config)
