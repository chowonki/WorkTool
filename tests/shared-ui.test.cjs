// 공통 화면(Liquid Glass 스타일·화면 모드·메뉴) 테스트
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.join(__dirname,'..');
const read=p=>fs.readFileSync(path.join(root,p),'utf8');
const glass=read('assets/mac-glass.css');
const pages=['index.html','dispatch.html','transfer.html','quick-guide.html','dispatch-guide.html','transfer-guide.html','changelog.html'];

test('모든 화면이 같은 스타일과 화면 모드 스크립트를 같은 버전으로 불러와요',()=>{
 const versions=new Set();
 for(const p of pages){
  const html=read(p);
  const m=html.match(/href="\.\/assets\/mac-glass\.css\?v=([\w-]+)"/);assert.ok(m,p+' 스타일 없음');versions.add(m[1]);
  assert.match(html,/src="\.\/assets\/theme\.js\?v=[\w-]+"/,p);
  assert.match(html,/<body[^>]*class="[^"]*mac-ui/,p);
 }
 assert.equal(versions.size,1,'스타일 버전이 화면마다 달라요: '+[...versions]);
});
test('두 도구의 위쪽 메뉴: 홈·탁송체크·이체파일·그림 사용법, 현재 화면 표시',()=>{
 for(const [p,cur,anchor] of [['dispatch.html','./dispatch.html','dispatch'],['transfer.html','./transfer.html','transfer']]){
  const nav=read(p).match(/<nav class="toolnav"[\s\S]*?<\/nav>/)[0];
  for(const href of ['./index.html','./dispatch.html','./transfer.html',`./quick-guide.html#${anchor}`])assert.ok(nav.includes(`href="${href}"`),p+' '+href);
  assert.ok(nav.includes(`aria-current="page" href="${cur}"`),p+' 현재 화면 표시');
 }
});
test('다크 모드: 화면 모드 선택(자동·라이트·다크)과 다크 색이 있어요',()=>{
 const theme=read('assets/theme.js');
 for(const s of ["'system','자동'","'light','라이트'","'dark','다크'",'prefers-color-scheme: dark','localStorage'])assert.ok(theme.includes(s),s);
 for(const s of [':root[data-theme=dark]','html[data-theme=dark] body.mac-ui .panel','html[data-theme=dark] body.mac-ui select'])assert.ok(glass.includes(s),s);
});
test('유리 효과는 바깥 판에만 쓰고 안쪽 상자는 겹쳐 흐리지 않아요',()=>{
 assert.match(glass,/:is\(\.panel,\.card,\.stepper,\.wizard-header\) :is\([^)]*\.box[^)]*\)\{backdrop-filter:none/);
 assert.ok(glass.includes('--r-outer')&&glass.includes('--r-inner')&&glass.includes('--r-ctl'),'모서리 둥글기 단계');
});
test('작은 화면·움직임 줄이기·투명도 줄이기·인쇄에 대응해요',()=>{
 for(const s of ['@media(max-width:650px)','@media(max-width:950px)','prefers-reduced-motion:reduce','prefers-reduced-transparency:reduce','@media print',':focus-visible'])assert.ok(glass.includes(s),s);
 assert.match(glass,/@media\(max-width:650px\)\{[^}]*\.toolnav\{flex-wrap:nowrap;overflow-x:auto/,'휴대폰 메뉴 한 줄');
});
test('예전 단계별 이체 페이지는 현재 이체파일 화면으로 보내요',()=>{
 for(const p of ['transfer-input.html','transfer-review.html','transfer-download.html']){
  const html=read(p);
  assert.match(html,/http-equiv="refresh" content="0; url=\.\/transfer\.html"/,p);
  assert.match(html,/href="\.\/transfer\.html"/,p);
 }
});
test('없는 파일을 가리키는 링크·스타일·스크립트가 없어요',()=>{
 for(const p of [...pages,'transfer-input.html','transfer-review.html','transfer-download.html']){
  for(const m of read(p).matchAll(/(?:href|src)="\.\/([^"#?]+)/g))assert.ok(fs.existsSync(path.join(root,m[1])),p+' → '+m[1]);
 }
});
test('사용법 문서가 지금 화면에 없는 기능을 설명하지 않아요',()=>{
 const docs=['README.md','quick-guide.html','dispatch-guide.html','transfer-guide.html'];
 const removed=['수동확인','중복 검토 필요','표시할 탁송건 선택','선택 처리 완료','접수일시','4시간','마지막 다운로드 내역 복원','되돌리기','새 이체 작업 시작','검토 필요한 행만 보기','체험 데이터','예시로 연습하기'];
 for(const p of docs){const text=read(p);for(const w of removed)assert.ok(!text.includes(w),`${p}에 '${w}' 설명이 남아 있어요`);}
});
test('안내 문구는 해요체예요 (합니다·습니다 없음)',()=>{
 for(const p of [...pages,'README.md']){
  const text=read(p).replace(/<script[\s\S]*?<\/script>/g,s=>s.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g,''));
  const hit=text.match(/[가-힣]+(?:니다|십시오)/);assert.ok(!hit,`${p}: '${hit&&hit[0]}'`);
 }
});
test('예전 사용 설명서 주소는 그림 사용법으로 보내요',()=>{
 assert.match(read('guide.html'),/http-equiv="refresh" content="0; url=\.\/quick-guide\.html"/);
 for(const p of ['index.html','dispatch.html','transfer.html','quick-guide.html'])assert.ok(!/href="\.\/guide\.html"/.test(read(p)),p+'에 사용 설명서 링크가 남아 있어요');
});
