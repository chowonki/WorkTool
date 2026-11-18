// 탁송체크 브라우저 테스트 (가상 업체 엑셀로 조회 → 결과 캡처)
// 실행: python3 scripts/make-guide-fixtures.py
//       python3 -m http.server 8765 --bind 127.0.0.1 &  CHROME_BIN=$(command -v google-chrome) node scripts/e2e-dispatch.cjs
let pw;try{pw=require('playwright-core');}catch{pw=require('playwright');}
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const BASE=process.env.BASE_URL||'http://127.0.0.1:8765',FX=path.join(__dirname,'fixtures');
let failed=0;const ok=(c,m)=>{console.log((c?'PASS ':'FAIL ')+m);if(!c)failed++;};
(async()=>{
 const browser=await pw.chromium.launch({executablePath:process.env.CHROME_BIN||undefined,env:{...process.env,LANG:'C.UTF-8',LC_ALL:'C.UTF-8'}});
 const ctx=await browser.newContext({viewport:{width:1366,height:900},acceptDownloads:true,permissions:['clipboard-read','clipboard-write']});
 const page=await ctx.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto(BASE+'/dispatch.html');await page.waitForTimeout(500);
 ok(!(await page.$('#demo'))&&!(await page.$('#demoBanner')),'체험 데이터 버튼·배너 없음');
 await page.fill('#input',fs.readFileSync(path.join(FX,'차량목록.txt'),'utf8'));
 await page.setInputFiles('#fileInput',['한빛탁송_가상.xlsx','오픈마일_가상.xlsx','렌톡_가상.xlsx'].map(f=>path.join(FX,f)));
 await page.waitForFunction(()=>document.querySelectorAll('#fileList li').length===3,null,{timeout:20000});
 ok(/한빛탁송 양식/.test(await page.textContent('#fileList')),'숫자 문자 참조(&#…;)로 저장된 엑셀도 읽음');
 await page.click('[data-capture=png]');await page.waitForTimeout(300);
 ok(/먼저 차량 조회/.test(await page.textContent('#toast')),'조회 전 캡처는 안내만');
 await page.click('#run');await page.waitForTimeout(1200);
 ok(await page.$$eval('#body tr',t=>t.length)===10,'10대 조회');
 ok(/점선 밑줄/.test(await page.textContent('.copyhint')),'복사 안내 문구 표시');
 for(const [sel,want] of [['td.plate','12서7567'],['td.amount.fuel','12350'],['td:nth-child(2)','2026-11-03 09:30'],['td:nth-child(5)','28000'],['td:nth-child(6)','2800']]){
  await page.click('#body tr:first-child '+sel);await page.waitForTimeout(120);
  ok(await page.evaluate(()=>navigator.clipboard.readText())===want,'눌러서 복사: '+want);
 }
 ok((await page.$eval('#body tr:first-child td.plate',e=>getComputedStyle(e).fontWeight))<=600,'차량번호 굵기 600 이하');
 await page.waitForTimeout(1600);
 const [dl]=await Promise.all([page.waitForEvent('download'),page.click('[data-capture=png]')]);
 const file=path.join(os.tmpdir(),'dispatch-capture.png');await dl.saveAs(file);
 const png=fs.readFileSync(file);
 ok(png.subarray(1,4).toString()==='PNG','캡처 저장: PNG 파일');
 ok(/^탁송조회_캡처_\d{8}_\d{6}\.png$/.test(dl.suggestedFilename()),'파일 이름 '+dl.suggestedFilename());
 const h=png.readUInt32BE(20);ok(h>(92+40+4*36)*2,'기본 범위(금액 확인 4대) 이미지 높이 '+h);
 await page.waitForTimeout(400);await page.click('[data-capture=copy]');await page.waitForTimeout(600);
 const types=await page.evaluate(async()=>(await navigator.clipboard.read()).flatMap(i=>i.types));
 ok(types.includes('image/png'),'캡처 복사: 클립보드에 이미지');
 await page.click('#body tr:first-child input[data-pick]');
 await page.waitForTimeout(1600);
 const [dl2]=await Promise.all([page.waitForEvent('download'),page.click('[data-capture=png]')]);
 const f2=path.join(os.tmpdir(),'dispatch-capture2.png');await dl2.saveAs(f2);
 ok(fs.readFileSync(f2).readUInt32BE(20)<h,'체크한 줄만 캡처');
 ok(errors.length===0,'페이지 오류 없음 '+errors.join(' / '));
 await browser.close();if(failed){console.error(failed+'개 실패');process.exit(1);}
})().catch(e=>{console.error(e);process.exit(1);});
