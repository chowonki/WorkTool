// 하나은행 이체파일(transfer.html) — 추출 규칙과 엑셀 작성기 단위 테스트
// 화면 동작(검증·분할·다운로드)은 scripts/e2e-transfer.cjs 브라우저 테스트에서 확인해요.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {readFileSync}=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');

const html=readFileSync(path.join(__dirname,'../transfer.html'),'utf8');
const scripts=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]);
const ruleSource=scripts.find(s=>s.includes('function parseOne('));
const appSource=scripts.find(s=>s.includes('function check(r)'));
assert.ok(ruleSource&&appSource,'transfer.html 스크립트 구조가 바뀌었어요');

const ctx=vm.createContext({TextEncoder,Blob,decodeURIComponent});
vm.runInContext(ruleSource+'\nglobalThis.__t={parseKoreanAmount,parseOne,isDateLine,xmlSafe,xml,crc32,zip,bankOf,findPlate,holderOf};',ctx);
const t=ctx.__t;
const items=text=>JSON.parse(JSON.stringify(t.parseOne(text).items));

test('금액 표기: 원·만·억과 한글 숫자를 정확한 원 단위로 읽어요',()=>{
 for(const [text,want] of [['35,000원','35000'],['3만5천원','35000'],['1000만원','10000000'],['천만원','10000000'],['1억 2천만원','120000000'],['2억5천만원','250000000'],['금액: 10000000','10000000'],['₩35,000','35000'],['80000','80000']]){
  assert.equal(t.parseKoreanAmount(text),want,text);
 }
});
test('금액 표기: 잘못 읽으면 엉뚱한 금액이 되는 표기는 비워서 확인하게 해요',()=>{
 // 소수·음수·분수·지수·쉼표 위치 오류·여러 금액
 for(const text of ['','안녕하세요','35000원 / 40000원','1.5만원','.5만원','3.5억','35,000.50원','35000.00원','35,00원','35,00원 500원','-35000원','- 35000원','−35,000원','금액: -35000원','금액 -천만원','₩-35000','1/2만원','1e5원']){
  assert.equal(t.parseKoreanAmount(text),'',text);
  if(text!=='- 35000원')assert.equal(items(`국민은행\n123-456-789012\n${text}`)[0]?.amount??'','',text);
 }
 // 줄 맨 앞 "- "는 Slack 목록 기호라서 금액으로 읽어요
 assert.equal(items('국민은행\n123-456-789012\n- 35,000원')[0].amount,'35000');
});
test('금액 표기: 같은 줄의 날짜나 구분용 하이픈은 금액에 영향을 주지 않아요',()=>{
 for(const [text,want] of [['35000원 (2026.11.17 요청)','35000'],['2026.11.17 35,000원','35000'],['오픈마일 운송비 - 80,000원','80000'],['35,000원(VAT포함)','35000']])assert.equal(t.parseKoreanAmount(text),want,text);
});

test('스레드 추출: 은행·계좌·금액·차량번호를 건별로 나눠요',()=>{
 const got=items(`현대 쏘나타 123가4567\nhttps://example.com/vehicle/987654321\n국민은행\n123-456-789012\n35,000원\n\n기아 K5 45나6789\n신한은행\n110-123-456789\n3만5천원`);
 assert.equal(got.length,2);
 assert.deepEqual(got.map(x=>[x.bank,x.account,x.amount,x.plate]),[['국민','123456789012','35000','123가4567'],['신한','110123456789','35000','45나6789']]);
});
test('스레드 추출: 링크 안의 숫자는 계좌·차량번호로 보지 않아요',()=>{
 const got=items(`https://slack.com/archives/C123/p1700000000123456\n우리은행\n1002-345-678901\n80,000원`);
 assert.equal(got.length,1);assert.equal(got[0].account,'1002345678901');assert.equal(got[0].amount,'80000');
});
test('날짜·시간 줄은 계좌번호로 읽지 않아요',()=>{
 for(const line of ['2026-11-17','2026.11.17 14:30','2026년 11월 17일','2026-11-17 (화) 오후 3:45'])assert.ok(t.isDateLine(line),line);
 for(const line of ['123-456-789012','1002-345-678901','110123456789'])assert.ok(!t.isDateLine(line),line);
 const got=items(`2026-11-17 14:30\n쏘나타 123가4567\n국민은행\n2026.11.17\n123-456-789012\n35,000원`);
 assert.equal(got.length,1);assert.equal(got[0].account,'123456789012');
});
test('예금주 줄에 은행 이름이 섞여도 은행 줄로 보지 않아요',()=>{
 const got=items(`쏘나타 123가4567\n웰컴저축은행\n123-456-789012\n예금주 : 홍길동웰컴저축은행\n35,000원`);
 assert.equal(got.length,1);assert.equal(got[0].bank,'저축');
 assert.equal(t.holderOf('예금주 : 홍길동'),'홍길동');
});
test('차량번호가 여러 개면 임의로 고르지 않고 비워 둬요',()=>{
 const res=t.parseOne(`123가4567 45나6789 같이 처리\n국민은행\n123-456-789012\n35,000원`);
 assert.equal(res.ambiguous,true);assert.equal(res.items[0].plate,'');
});
test('은행 약칭은 하나은행 코드표 이름으로 맞춰요',()=>{
 for(const [text,want] of [['KB국민','국민'],['NH농협','농협'],['카뱅','카카오뱅크'],['IBK기업','기업'],['새마을금고','새마을금고'],['iM뱅크','iM뱅크(대구)']])assert.equal(t.bankOf(text),want,text);
 assert.equal(t.bankOf('없는은행'),'');
});

test('엑셀 XML: 제어문자·깨진 문자를 지워 파일이 깨지지 않아요',()=>{
 assert.equal(t.xmlSafe('A\u0001B\u000bC'),'ABC');
 assert.equal(t.xmlSafe('줄\n바꿈\t탭'),'줄 바꿈 탭');
 assert.equal(t.xmlSafe('x\uD800y'),'xy');
 assert.equal(t.xml('<a&b>'),'&lt;a&amp;b&gt;');
});
test('엑셀 파일: 올바른 ZIP 구조와 CRC32를 만들어요',async()=>{
 assert.equal(t.crc32(new TextEncoder().encode('123456789')),0xcbf43926);
 const blob=t.zip([['a.txt','hello'],['b.xml','<x/>']]);
 const bytes=new Uint8Array(await blob.arrayBuffer());
 const u32=i=>bytes[i]|bytes[i+1]<<8|bytes[i+2]<<16|bytes[i+3]<<24;
 assert.equal(u32(0)>>>0,0x04034b50,'local header');
 assert.equal(u32(bytes.length-22)>>>0,0x06054b50,'end of central directory');
 assert.equal(bytes[bytes.length-22+10],2,'entry count');
});

// 화면 코드의 안전장치가 빠지지 않았는지 확인 (실제 동작은 브라우저 테스트)
test('안전장치: 분할 상한·재추출 중복 방지·중복 송금 경고·검토 재확인이 있어요',()=>{
 for(const marker of [
  "if(full+(rest?1:0)>MAX)return [r];",
  "r.threadId!==p.t.id",
  "내용이 같아 건너뛰었어요",
  "두 번 송금 주의",
  "휴대폰 번호 형태 계좌",
  "ackSig!==dataSig()",
  "downloading||",
  "'beforeunload'",
  "Number.isSafeInteger(v)?`<c r=",
  "TEXT_MAX=50000"
 ])assert.ok(appSource.includes(marker)||ruleSource.includes(marker),marker);
});
test('하나은행 엑셀 열 순서는 A 입금은행 ~ F CMS코드예요',()=>{
 assert.match(appSource,/\['입금은행','입금계좌번호','입금액\(원\)','출금통장표시','입금통장표시','CMS코드'\]/);
});
test('모든 은행 선택지에 하나은행 코드가 있어요',()=>{
 const codes=Function('return '+appSource.match(/const BANK_CODES=(\{[\s\S]*?\});/)[1])();
 const groups=Function('return '+appSource.match(/const BANK_GROUPS=(\[[\s\S]*?\]);\nconst BANK_OPTIONS/)[1])();
 for(const [,list] of groups)for(const b of list)assert.match(codes[b]||'',/^\d{3}$/,b);
 const aliasTargets=new Set(Function('return '+ruleSource.match(/const banks=(\[[\s\S]*?\]);/)[1])().map(x=>x[1]));
 for(const b of aliasTargets)assert.ok(codes[b],'별칭 대상 '+b+' 이 드롭다운에 없음');
});
