// 탁송체크(dispatch.html) — 화면이 실제로 쓰는 조회 규칙(core-source) 테스트
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {readFileSync}=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');

const page=readFileSync(path.join(__dirname,'../dispatch.html'),'utf8');
const core=page.match(/<script id="core-source"[^>]*>([\s\S]*?)<\/script>/)[1];
function load(){const ctx=vm.createContext({});vm.runInContext(core,ctx);return ctx.DispatchCore;}
const C=load();
const plain=v=>JSON.parse(JSON.stringify(v));

// 업체 시트 만들기: 업체별 고정 열(A: K일자·L시간·AP비용, B: C일자·U비용, C: D일자·J캐리어·S비용)
const HEAD={
 A:{0:'차량번호',1:'상태',2:'주유/부대비용 총액',3:'출발지 주소'},
 B:{0:'차량번호',1:'상태',3:'주유비',4:'출발지'},
 C:{0:'출발지 차량번호',1:'운행상태',2:'출발지 주소(현재차량 상세위치)'}
};
function sheet(vendor,rows,{name='자료',pad=0}={}){
 const head=Array(45).fill('');for(const [i,v] of Object.entries(HEAD[vendor]))head[i]=v;
 const body=rows.map(r=>{const row=Array(45).fill('');
  row[0]=r.plate;row[1]=r.state??'탁송완료';
  if(vendor==='A'){row[2]=r.cost??'';row[3]=r.origin??'';row[10]=r.date??'';row[11]=r.time??'';row[41]=r.fee??'';}
  if(vendor==='B'){row[3]=r.cost??'';row[4]=r.origin??'';row[2]=r.date??'';row[20]=r.fee??'';}
  if(vendor==='C'){row[2]=r.origin??'';row[3]=r.date??'';row[9]=r.carrier??'';row[18]=r.fee??'';}
  return row;});
 return {name,rows:[...Array(pad).fill(['제목 위 안내문']),head,...body]};
}
const read=(vendor,rows,file)=>C.readSheets([sheet(vendor,rows)],file||vendor+'.xlsx');
const one=(plate,files)=>plain(C.match([{plate,date:''}],files)[0]);

test('차량목록: 날짜·시간과 차량번호를 함께 읽고 잘못된 줄은 따로 모아요',()=>{
 const r=plain(C.parseInput('전체 3건\n2026-10-23 10:00  12서7567\n55노4983\n2026-02-30 10:00 12가3456\n아무말\n55노4983'));
 assert.deepEqual(r.vehicles.map(v=>[v.plate,v.date]),[['12서7567','2026-10-23 10:00'],['55노4983',''],['55노4983','']]);
 assert.equal(r.invalid.length,2);
 assert.equal(r.duplicates,1);
});
test('차량번호: 자모가 풀어진 번호도 한 글자로 합쳐서 비교해요',()=>{
 assert.equal(C.plate('12ㅅㅓ7567'),'12서7567');
 assert.equal(C.plate(' 서울12 가 3456 '),'서울12가3456');
 assert.ok(C.valid('123가4567'));assert.ok(C.valid('서울12가3456'));assert.ok(!C.valid('1234567'));
});
test('금액: 쉼표·원 표시는 지우고 숫자가 아니면 비워요',()=>{
 assert.equal(C.money('35,000원'),35000);assert.equal(C.money(' 0 '),0);
 assert.equal(C.money(''),null);assert.equal(C.money('확인중'),null);
 assert.equal(C.round10(35004),35000);assert.equal(C.round10(35005),35010);
});

test('업체 양식: 제목 줄 위치와 상관없이 한빛·오픈마일·렌톡을 알아봐요',()=>{
 const f=C.readSheets([sheet('A',[{plate:'12서7567'}],{name:'한빛',pad:3}),sheet('B',[{plate:'55노4983'}],{name:'오픈'}),sheet('C',[{plate:'77다1234'}],{name:'렌톡'})],'3사.xlsx');
 assert.deepEqual(plain(f.sheets),['한빛','오픈','렌톡']);
 assert.deepEqual(plain(f.records.map(r=>r.vendor)),['A','B','C']);
 assert.throws(()=>C.readSheets([{name:'x',rows:[['이름','나이']]}],'x.xlsx'),/양식을 찾지 못했습니다/);
});
test('한빛 빈 주유비는 0원, 오픈마일 빈 주유비는 미입력으로 둬요',()=>{
 assert.equal(read('A',[{plate:'12서7567',cost:''}]).records[0].cost,0);
 assert.equal(read('B',[{plate:'12서7567',cost:''}]).records[0].cost,null);
 assert.equal(read('B',[{plate:'12서7567',cost:'0'}]).records[0].cost,0);
});
test('차량번호가 아닌 줄은 건너뛰고 개수만 세요',()=>{
 const f=read('A',[{plate:'12서7567'},{plate:'합계',state:'x'}]);
 assert.equal(f.records.length,1);assert.equal(f.ignored,1);
});

test('조회 상태: 없음·미완료·금액 확인·비용 확인',()=>{
 assert.equal(one('99하9999',[read('A',[{plate:'12서7567'}])]).status,'missing');
 assert.equal(one('12서7567',[read('A',[{plate:'12서7567',state:'탁송중'}])]).status,'incomplete');
 const ok=one('12서7567',[read('A',[{plate:'12서7567',cost:'35,004'}])]);
 assert.equal(ok.status,'confirmed');assert.equal(ok.amount,35000);assert.equal(ok.vendor,'한빛탁송');
 assert.equal(one('12서7567',[read('B',[{plate:'12서7567',cost:''}])]).status,'cost');
});
test('업체가 달라도 탁송일자가 가장 최신인 기록 하나를 써요',()=>{
 const r=one('12서7567',[read('A',[{plate:'12서7567',cost:'10000',date:'2026-11-01'}]),read('B',[{plate:'12서7567',cost:'20000',date:'2026-11-05'}])]);
 assert.equal(r.vendor,'오픈마일');assert.equal(r.amount,20000);assert.equal(r.dispatchDate,'2026-11-05');
});
test('렌톡: 캐리어(J열 Y)는 문의에서 빼고, 나머지 완료 건은 슬랙 문의로 분류해요',()=>{
 const y=one('77다1234',[read('C',[{plate:'77다1234',carrier:'Y'}])]);
 assert.equal(y.status,'carrier');assert.equal(y.carrierType,'캐리어탁송');assert.equal(y.cInquiry,false);
 const n=one('77다1234',[read('C',[{plate:'77다1234',carrier:'N'}])]);
 assert.equal(n.status,'slack');assert.equal(n.carrierType,'일반탁송');assert.equal(n.cInquiry,true);
 assert.equal(one('77다1234',[read('C',[{plate:'77다1234',carrier:''}])]).carrierType,'확인 필요');
});
test('제주 출발 렌톡 건을 표시해요',()=>{
 assert.equal(one('77다1234',[read('C',[{plate:'77다1234',origin:'제주특별자치도 제주시'}])]).jeju,true);
});
test('탁송비용 부가세는 10%이고, 기록마다 비용이 다르면 직접 고르게 해요',()=>{
 const r=one('12서7567',[read('A',[{plate:'12서7567',fee:'50000',date:'2026-11-01'}])]);
 assert.equal(r.dispatchFee,50000);assert.equal(r.vat,5000);assert.equal(r.feeConflict,false);
 const files=[read('A',[{plate:'12서7567',fee:'50000',date:'2026-11-01'}]),read('B',[{plate:'12서7567',fee:'60000',date:'2026-11-02'}])];
 const row=C.match([{plate:'12서7567',date:''}],files)[0];
 assert.equal(row.feeConflict,true);assert.equal(row.dispatchFee,null);assert.ok(C.hasUnresolvedChoice(row));
 const pick=row.feeCandidates.find(c=>c.value===50000);
 assert.ok(C.useCandidate(row,'fee',pick.id));
 assert.equal(row.dispatchFee,50000);assert.equal(row.vat,5000);assert.ok(!C.hasUnresolvedChoice(row)||row.scheduleConflict);
});
test('차량목록에 같은 번호가 두 번 있으면 입력 중복으로 표시해요',()=>{
 const rows=plain(C.match([{plate:'12서7567',date:''},{plate:'12서7567',date:''}],[read('A',[{plate:'12서7567'}])]));
 assert.ok(rows.every(r=>r.inputDuplicate));
 assert.equal(C.unique(rows).length,1);
});
test('TXT 저장: 엑셀에서 바로 열리게 BOM과 탭 구분을 쓰고, 칸 안 줄바꿈은 지워요',()=>{
 const rows=C.match([{plate:'12서7567',date:''}],[read('A',[{plate:'12서7567',origin:'서울\n강남'}])]);
 const txt=C.textExport(rows);
 assert.ok(txt.startsWith('﻿'));
 const lines=txt.slice(1).split('\r\n');assert.equal(lines.length,2);
 assert.equal(lines[0].split('\t').length,lines[1].split('\t').length);
 assert.ok(lines[1].includes('서울 강남'));
});
