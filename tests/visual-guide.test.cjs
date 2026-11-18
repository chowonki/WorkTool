const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.join(__dirname,'..');
const read=name=>fs.readFileSync(path.join(root,name),'utf8');
const guide=read('quick-guide.html');
const home=read('index.html');
const dispatch=read('dispatch.html');
const transfer=read('transfer.html');

test('visual quick guide is linked from both tools and the home screen',()=>{
 assert.match(home,/href="\.\/quick-guide\.html"/);
 assert.match(dispatch,/href="\.\/quick-guide\.html#dispatch"/);
 assert.match(transfer,/href="\.\/quick-guide\.html#transfer"/);
 assert.match(guide,/id="dispatch"/);
 assert.match(guide,/id="transfer"/);
 assert.match(guide,/실제 WorkTool 화면/);
 assert.match(guide,/이체파일은 송금 예약이나 실행이 아니에요/);
});
test('screen guides link real PNG screenshots for reference',()=>{
 for(const filename of ['dispatch-screen.png','transfer-screen.png','transfer-Review.png','transfer-Download.png']){
  const name='guide/'+filename;
  const contents=fs.readFileSync(path.join(root,name));
  assert.ok(contents.byteLength>20000,name+' must not be empty/blank');
  assert.equal(contents.subarray(0,8).toString('hex'),'89504e470d0a1a0a',name+' must be PNG');
  assert.ok(guide.includes('./'+name),name+' must appear in the quick guide');
 }
});
test('the quick guide has working demo screens with numbered badges and keeps actual tool navigation',()=>{
 const demos=guide.match(/<div class="mock live" role="group" aria-label="[^"]+"/g)||[];
 assert.equal(demos.length,2,'탁송체크·이체파일 체험 화면이 하나씩 있어야 해요');
 assert.match(guide,/<script src="\.\/assets\/guide-demo\.js[^"]*" defer><\/script>/);
 assert.ok(!/<img\s/.test(guide),'그림 사용법은 스크린샷 대신 체험 화면을 보여 줘요');
 const dispatchMock=guide.slice(guide.indexOf('id="dmock"'),guide.indexOf('id="transfer"'));
 for(let n=1;n<=6;n++)assert.ok(dispatchMock.includes('<b class="n" aria-hidden="true">'+n+'</b>')||n>=4,'탁송체크 체험 번호 '+n);
 const demo=read('assets/guide-demo.js');
 for(const label of ['차량 조회하기','캡처 복사','이체정보 추출하기','다음: 이체파일 만들기','하나은행 이체파일 다운로드','송금 코멘트'])assert.ok(guide.includes(label)||demo.includes(label),label);
 assert.match(demo,/체험 화면이라 실제로 만들지는 않아요/);
 assert.ok(!/localStorage|fetch\(/.test(demo),'체험 화면은 아무것도 저장하거나 보내지 않아요');
 assert.match(guide,/href="\.\/dispatch\.html"/);
 assert.match(guide,/href="\.\/transfer\.html"/);
 assert.match(guide,/target="_blank" rel="noopener"/);
});
test('dispatch screenshot guide names the real internal sources and preserves safe order',()=>{
 const detail=read('dispatch-guide.html');
 const required=['탁송출발 주유비 미입력','탁송기한','한빛탁송','오픈마일','렌톡','요청시각','출발시각','차량번호를 추출','업체 파일 첨부'];
 for(const term of required){
  assert.ok(guide.includes(term),'quick guide missing '+term);
  assert.ok(detail.includes(term),'detailed guide missing '+term);
 }
 const script=guide.indexOf('원본 준비 1'),vendor=guide.indexOf('원본 준비 2'),lookup=guide.indexOf('차량 조회하기');
 assert.ok(script>=0&&vendor>script&&lookup>vendor,'beginner workflow should follow actual preparation order');
 // 도구 화면에서는 그림 사용법으로 바로 갈 수 있어요
 assert.match(dispatch,/href="\.\/quick-guide\.html#dispatch"/);
});

test('all four annotated screenshots are valid and included in both help pages',()=>{
 const dispatchHelp=read('dispatch-guide.html'),transferHelp=read('transfer-guide.html');
 for(const name of ['dispatch-annotated.png','transfer-input-annotated.png','transfer-review-annotated.png','transfer-download-annotated.png']){
  const relative='guide/'+name;
  const bytes=fs.readFileSync(path.join(root,relative));
  assert.equal(bytes.subarray(0,8).toString('hex'),'89504e470d0a1a0a',relative);
  assert.ok(bytes.length>30000,relative+' must contain annotation artwork');
  assert.ok(guide.includes('href="./'+relative+'"'),relative+' link missing in quick guide');
  assert.ok((name.startsWith('dispatch')?dispatchHelp:transferHelp).includes('./'+relative),relative+' missing in detailed guide');
 }
 assert.match(guide,/주석 없는 원본/);
 assert.match(guide,/직접 눌러 보는 체험 화면/);
});
