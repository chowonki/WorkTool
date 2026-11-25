// 업체 양식 변경 감지(탁송체크)와 오늘 받은 이체 건 다시 받기 경고(이체파일) 규칙 테스트
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {readFileSync}=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');

const dispatch=readFileSync(path.join(__dirname,'../dispatch.html'),'utf8');
const core=dispatch.match(/<script id="core-source"[^>]*>([\s\S]*?)<\/script>/)[1];
const ctx=vm.createContext({});vm.runInContext(core,ctx);
const C=ctx.DispatchCore;
const plain=v=>JSON.parse(JSON.stringify(v));

function sheetFor(vendor,rows){
 const head=Array(45).fill('');
 if(vendor==='A')Object.assign(head,{0:'차량번호',1:'상태',2:'주유/부대비용 총액',3:'출발지 주소'});
 if(vendor==='B')Object.assign(head,{0:'차량번호',1:'상태',3:'주유비',4:'출발지'});
 if(vendor==='C')Object.assign(head,{0:'출발지 차량번호',1:'운행상태',2:'출발지 주소(현재차량 상세위치)'});
 return [{name:'탁송기한',rows:[head,...rows.map(cells=>{const r=Array(45).fill('');Object.assign(r,cells);return r;})]}];
}

test('업체 양식 점검: 평소 양식이면 경고하지 않아요',()=>{
 const ok=C.readSheets(sheetFor('C',[
  {0:'12가3456',1:'운행완료',3:'2026-11-20 09:30',9:'N',18:'55000'},
  {0:'34나5678',1:'운행완료',3:'2026-11-21',9:'Y',18:'60,000'}
 ]),'렌톡.xlsx');
 assert.deepEqual(plain(ok.warnings),[]);
 assert.equal(ok.records.length,2);
 const blank=C.readSheets(sheetFor('A',[{0:'12가3456',1:'탁송완료',2:'1000'}]),'한빛.xlsx');
 assert.deepEqual(plain(blank.warnings),[],'빈 열은 경고하지 않아요');
});

test('업체 양식 점검: 열이 밀려 날짜·금액·캐리어 값이 엉뚱하면 경고해요',()=>{
 // 렌톡에서 열 하나가 끼어들어 D열에 주소, S열에 글자, J열에 날짜가 들어온 경우
 const shifted=C.readSheets(sheetFor('C',[
  {0:'12가3456',1:'운행완료',3:'서울시 강남구',9:'2026-11-20',18:'홍길동'},
  {0:'34나5678',1:'운행완료',3:'부산시 해운대구',9:'2026-11-21',18:'김철수'}
 ]),'렌톡.xlsx');
 const w=plain(shifted.warnings);
 assert.equal(w.length,3);
 assert.match(w[0],/탁송일정\(D열\) 2건 중 2건/);
 assert.match(w[1],/탁송비용\(S열\)/);
 assert.match(w[2],/캐리어 구분\(J열\)/);
 assert.equal(shifted.records.length,2,'경고만 하고 기록은 그대로 읽어요');
 // 절반 이하만 이상하면 경고하지 않아요
 const mostlyOk=C.readSheets(sheetFor('B',[
  {0:'12가3456',1:'탁송완료',2:'2026-11-20',20:'30000'},
  {0:'34나5678',1:'탁송완료',2:'2026-11-21',20:'30000'},
  {0:'56다7890',1:'탁송완료',2:'메모',20:'30000'}
 ]),'오픈마일.xlsx');
 assert.deepEqual(plain(mostlyOk.warnings),[]);
});

const transfer=readFileSync(path.join(__dirname,'../transfer.html'),'utf8');
const fn=name=>transfer.match(new RegExp(`function ${name}\\([\\s\\S]*?\\r?\\n}\\r?\\n`))[0];
const tctx=vm.createContext({});vm.runInContext(fn('sentKey')+fn('sentMatches')+'globalThis.__s={sentKey,sentMatches};',tctx);
const S=tctx.__s;

test('오늘 받은 이체 건: 같은 은행·계좌·금액만 다시 받기 경고 대상이에요',()=>{
 const a={bank:'국민',account:'123456789012',amount:'35000'};
 const key=S.sentKey(a);
 assert.match(key,/^[0-9a-f]{16}$/);
 assert.ok(!key.includes('123456789012'),'계좌번호 원문을 남기지 않아요');
 assert.equal(S.sentKey({...a}),key);
 assert.notEqual(S.sentKey({...a,amount:'35001'}),key);
 assert.notEqual(S.sentKey({...a,bank:'신한'}),key);
 const hits=plain(S.sentMatches([{...a,amount:'1'},a],[{key,at:'09:30'}]));
 assert.deepEqual(hits,[{row:1,at:'09:30'}]);
 assert.deepEqual(plain(S.sentMatches([a],[])),[]);
});
