// 하나은행 이체파일 실제 브라우저 테스트
// 실행: python3 -m http.server 8765 --bind 127.0.0.1 & CHROME_BIN=$(command -v google-chrome) node scripts/e2e-transfer.cjs
let pw;try{pw=require('playwright-core');}catch{pw=require('playwright');}
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const BASE=process.env.BASE_URL||'http://127.0.0.1:8765';
let failed=0;const ok=(cond,msg)=>{console.log((cond?'PASS ':'FAIL ')+msg);if(!cond)failed++;};

(async()=>{
 const browser=await pw.chromium.launch({executablePath:process.env.CHROME_BIN||undefined,env:{...process.env,LANG:'C.UTF-8',LC_ALL:'C.UTF-8'}}) // 한글 파일 이름이 'download'로 바뀌지 않게;
 const page=await browser.newPage({acceptDownloads:true,viewport:{width:1440,height:900}});
 const errors=[];page.on('pageerror',e=>errors.push(e.message));
 const rows=()=>page.$$eval('#rows tr',t=>t.length);
 const closeModal=()=>page.evaluate(()=>document.getElementById('modal').classList.remove('show'));
 const extract=async text=>{await page.click('.step[data-step="1"]');await page.fill('#threads textarea >> nth=-1',text);await page.click('#extract');await page.waitForTimeout(250);};
 await page.goto(BASE+'/transfer.html');

 // 1) 기본 추출 + 날짜 줄
 const T1='2026-11-17 14:30\n현대 쏘나타 123가4567\n국민은행\n2026.11.17\n123-456-789012\n35,000원\n\n기아 K5 45나6789\n신한은행\n110-123-456789\n3만5천원';
 await extract(T1);await closeModal();
 ok(await rows()===2,'두 건 추출');
 ok((await page.$$eval('#rows [data-k=account]',a=>a.map(x=>x.value)))[0]==='123456789012','날짜 줄을 계좌로 읽지 않음');

 // 2) 고쳐서 다시 추출 → 새 건만
 await page.click('.step[data-step="1"]');
 await page.fill('#threads textarea >> nth=0',T1+'\n\n오픈마일 운송비 77다1234\n우리은행\n1002-345-678901\n80,000원');
 await page.click('#extract');await page.waitForTimeout(250);await closeModal();
 ok(await rows()===3,'고쳐서 다시 추출하면 새 1건만 추가');

 // 3) 같은 스레드를 다른 칸에 → 건너뜀
 await page.click('.step[data-step="1"]');await page.click('#addThread');
 await page.fill('#threads textarea >> nth=1',T1+'\n\n오픈마일 운송비 77다1234\n우리은행\n1002-345-678901\n80,000원');
 await page.click('#extract');await page.waitForTimeout(250);
 ok(/내용이 같아 건너뛰었/.test(await page.textContent('#mBody')),'같은 스레드 재붙여넣기 건너뜀');await closeModal();
 ok(await rows()===3,'건수 그대로');

 // 4) 다른 문구로 같은 건 → 두 번 송금 경고
 await page.click('#addThread');await extract('메모 다름\n쏘나타 123가4567\n국민은행\n123-456-789012\n35,000원');await closeModal();
 const why=await page.$$eval('#rows .why',w=>w.map(x=>x.textContent).join('|'));
 ok(/두 번 송금 주의/.test(why),'같은 계좌·금액 중복 경고');
 await page.click('#rows tr:last-child input[data-sel]');page.once('dialog',d=>d.accept());await page.click('#delSel');await page.click('#mBtns .primary');
 ok(await rows()===3,'중복 줄 삭제');

 // 5) 금액: 위험한 표기는 오류, 1억 초과 분할, 너무 큰 금액은 멈추지 않고 오류
 const amt=page.locator('#rows [data-k=amount]').first(),status=page.locator('#rows .why').first();
 for(const v of ['3.5억','-35000','1/2만원']){await amt.fill(v);await amt.press('Tab');ok(/금액/.test(await status.textContent()),`"${v}" → 금액 오류`);}
 const t0=Date.now();await amt.fill('9000000억');await amt.press('Tab');await page.waitForTimeout(150);
 ok(Date.now()-t0<3000&&/100건 초과/.test(await status.textContent()),'거대 금액은 나누지 않고 오류');
 await amt.fill('250000000');await amt.press('Tab');await page.waitForTimeout(150);
 ok(await rows()===5,'2.5억 → 1억+1억+5천만 분할');
 ok(/분할 1\/3/.test(await page.locator('#rows .split').first().textContent()),'분할 표시');
 await page.click('#rows tr:nth-child(1) input[data-sel]');await page.click('#rows tr:nth-child(2) input[data-sel]');await page.click('#rows tr:nth-child(3) input[data-sel]');
 await page.click('#delSel');await page.click('#mBtns .primary');
 ok(await rows()===2,'분할 줄 삭제');

 // 6) 메모 제어문자, 휴대폰 형태 계좌
 await page.locator('#rows [data-k=inMemo]').first().fill('A\u0001B\u000bC');
 await page.locator('#rows [data-k=account]').nth(1).fill('01012345678');
 ok(/휴대폰/.test(await page.locator('#rows tr').nth(1).locator('.stcell').textContent()),'휴대폰 형태 계좌 검토');

 // 7) 3단계: 검토 확인 → 변경 시 해제 → 다운로드 1회
 await page.click('#toStep3');await page.waitForTimeout(150);
 ok(await page.$eval('#download',b=>b.disabled),'검토 확인 전 다운로드 막힘');
 await page.check('#ack');ok(!(await page.$eval('#download',b=>b.disabled)),'확인 후 다운로드 가능');
 await page.click('.step[data-step="2"]');await page.locator('#rows [data-k=cms]').first().fill('X1');await page.click('#toStep3');
 ok(!(await page.isChecked('#ack')),'내용이 바뀌면 검토 확인 해제');
 await page.check('#ack');
 const [download]=await Promise.all([page.waitForEvent('download'),page.click('#download')]);
 ok(await page.$eval('#download',b=>b.disabled),'다운로드 직후 연타 방지');
 const file=path.join(os.tmpdir(),'worktool-e2e.xlsx');await download.saveAs(file);
 const bytes=fs.readFileSync(file);
 ok(bytes.subarray(0,4).toString('hex')==='504b0304','xlsx(zip) 파일');
 ok(/하나은행_다계좌이체_\d{8}_\d{4}_/.test(download.suggestedFilename()),'파일 이름: '+download.suggestedFilename());
 const sheet=bytes.toString('utf8');
 ok(sheet.includes('<t>ABC</t>')&&!sheet.includes('A\u0001B'),'제어문자 없이 저장');

 // 8) 휴대폰 화면: 메뉴 한 줄, 단계 버튼 글자 깨짐 없음
 await page.setViewportSize({width:390,height:844});await page.click('.step[data-step="1"]');await page.waitForTimeout(150);
 const navH=await page.$eval('.toolnav',n=>n.getBoundingClientRect().height);
 ok(navH<70,'휴대폰 메뉴 한 줄 (높이 '+Math.round(navH)+'px)');
 const stepH=await page.$$eval('.step',s=>Math.max(...s.map(x=>x.getBoundingClientRect().height)));
 ok(stepH<90,'휴대폰 단계 버튼 높이 '+Math.round(stepH)+'px');

 ok(errors.length===0,'페이지 오류 없음 '+errors.join(' / '));
 await browser.close();
 if(failed){console.error(failed+'개 실패');process.exit(1);}
})().catch(e=>{console.error(e);process.exit(1);});
