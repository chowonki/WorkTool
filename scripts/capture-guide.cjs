// 사용법 스크린샷 촬영: 실제 WorkTool 화면을 가상 예시 데이터로 찍고,
// 번호를 붙일 요소의 위치를 scripts/guide-boxes.json에 저장해요. (번호·설명은 annotate-guide.py가 그려요)
// 실행: python3 -m http.server 8765 --bind 127.0.0.1 &  CHROME_BIN=$(command -v google-chrome) node scripts/capture-guide.cjs
let pw;try{pw=require('playwright-core');}catch{pw=require('playwright');}
const fs=require('node:fs'),path=require('node:path');
const BASE=process.env.BASE_URL||'http://127.0.0.1:8765';
const ROOT=path.join(__dirname,'..'),OUT=path.join(ROOT,'guide');
const W=1366;
// 가상 이체 요청 (실제 계좌 아님)
const THREAD='[이체 요청] 오늘 정산 부탁드려요.\n\n현대 쏘나타 123가4567\n국민은행\n123-456-789012\n35,000원\n\n기아 K5 45나6789\n신한은행\n110-123-456789\n3만5천원\n\n오픈마일 운송비\n우리은행\n1002-345-678901\n80,000원';

async function boxes(page,selectors){
 const out={};
 for(const [key,sel] of Object.entries(selectors)){
  // 선택자를 여러 개 주면 그 요소들을 모두 감싸는 상자를 써요.
  const list=Array.isArray(sel)?sel:[sel],bs=[];
  for(const one of list){const b=await page.locator(one).first().boundingBox();if(!b)throw Error(`요소를 찾지 못했어요: ${key} (${one})`);bs.push(b);}
  const x=Math.min(...bs.map(b=>b.x)),y=Math.min(...bs.map(b=>b.y)),r=Math.max(...bs.map(b=>b.x+b.width)),btm=Math.max(...bs.map(b=>b.y+b.height));
  out[key]=[Math.round(x),Math.round(y),Math.round(r-x),Math.round(btm-y)];
 }
 return out;
}
async function shot(page,file,selectors,clipHeight){
 await page.evaluate(()=>window.scrollTo(0,0));await page.waitForTimeout(150);
 const full=await page.evaluate(()=>document.documentElement.scrollHeight);
 const h=Math.min(clipHeight||full,full);
 await page.screenshot({path:path.join(OUT,file),clip:{x:0,y:0,width:W,height:h},fullPage:true,animations:'disabled'});
 return {file,size:[W,h],boxes:await boxes(page,selectors)};
}

(async()=>{
 const browser=await pw.chromium.launch({executablePath:process.env.CHROME_BIN||undefined,env:{...process.env,LANG:'C.UTF-8',LC_ALL:'C.UTF-8'}});
 const result={};
 try{
  const ctx=await browser.newContext({viewport:{width:W,height:900},deviceScaleFactor:1,colorScheme:'light'});
  const page=await ctx.newPage();

  // 탁송체크: 가상 업체 엑셀(scripts/fixtures, make-guide-fixtures.py로 생성)로 조회한 화면
  const FX=path.join(__dirname,'fixtures');
  await page.goto(BASE+'/dispatch.html',{waitUntil:'load'});await page.waitForTimeout(800);
  await page.fill('#input',fs.readFileSync(path.join(FX,'차량목록.txt'),'utf8'));
  await page.setInputFiles('#fileInput',['한빛탁송_가상.xlsx','오픈마일_가상.xlsx','렌톡_가상.xlsx'].map(f=>path.join(FX,f)));
  await page.waitForFunction(()=>document.querySelectorAll('#fileList li').length===3,null,{timeout:20000});
  await page.click('#run');await page.waitForTimeout(900);
  await page.evaluate(()=>{document.getElementById('toast').hidden=true;});
  result.dispatch=await shot(page,'dispatch-screen.png',{input:'.inputcard',files:'.filescard',run:'#run',stats:'.stats',tabs:'.filterbar .tabs',export:'.exportbar'},1830);

  // 이체파일 1단계: 가상 스레드를 넣은 화면
  await page.goto(BASE+'/transfer.html',{waitUntil:'load'});await page.waitForTimeout(300);
  await page.fill('#threads textarea',THREAD);await page.waitForTimeout(200);
  result.input=await shot(page,'transfer-screen.png',{text:'#threads textarea',add:'#addThread',extract:'#extract'},1090);

  // 2단계: 추출 직후 (차량번호 팝업은 닫음)
  await page.click('#extract');await page.waitForTimeout(400);
  await page.evaluate(()=>document.getElementById('modal').classList.remove('show'));
  await page.waitForTimeout(3300);
  result.review=await shot(page,'transfer-Review.png',{tiles:'#tiles2',table:['#rows tr:nth-child(3) td:nth-child(3)','#rows tr:nth-child(3) td:nth-child(7)'],memo:'.step2cols .side .box',next:'#toStep3'},1280);

  // 3단계: 다운로드 화면
  await page.click('#toStep3');await page.waitForTimeout(300);
  await page.check('#ack');await page.waitForTimeout(150);
  result.download=await shot(page,'transfer-Download.png',{summary:'#tiles3',preview:'.preview',ack:'#ackWrap',comments:'#commentBox',download:'#download'},1150);

  await ctx.close();
 }finally{await browser.close();}
 fs.writeFileSync(path.join(__dirname,'guide-boxes.json'),JSON.stringify(result,null,1)+'\n');
 console.log('촬영 완료:',Object.values(result).map(r=>`${r.file} ${r.size.join('×')}`).join(', '));
})().catch(e=>{console.error(e);process.exit(1);});
