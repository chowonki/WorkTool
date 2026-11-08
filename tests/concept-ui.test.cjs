const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const base=path.join(__dirname,'..');
const read=(p)=>fs.readFileSync(path.join(base,p),'utf8');
const pages={
 home:read('index.html'),
 dispatch:read('dispatch.html'),
 transfer:read('transfer.html')
};
const theme=read('assets/concept-worktool.css');

test('shared concept design is installed on both live tools and home',()=>{
 for(const [name,s] of Object.entries(pages)){
  assert.match(s,new RegExp('class="concept-'+name+'"'),name);
  assert.match(s,/href="\.\/assets\/concept-worktool\.css\?v=20261108-concept"/,name);
 }
 assert.match(pages.dispatch,/doc\.documentElement\.classList\.add\('worktool-dispatch-frame'\)/);
 assert.match(pages.dispatch,/doc\.head\.append\(sheet\)/);
 for(const selector of ['.worktool-dispatch-frame .workspace','.worktool-dispatch-frame .stats','.concept-transfer .input-grid','.concept-transfer .step-panel','.concept-home .tool']){
  assert.ok(theme.includes(selector),selector);
 }
});

test('real bank workflow still has its original forms, actions and safeguards',()=>{
 const html=pages.transfer;
 const controls=['threads','raw','parse','clear','addThread','stepInput','stepReview','stepDownload','outMemo','inMemo','cms','rows','download','transferComments'];
 for(const id of controls){
  const count=html.split('id="'+id+'"').length-1;
  assert.equal(count,1,'control '+id);
 }
 assert.match(html,/<div class="input-grid"><div class="input-work">/);
 assert.match(html,/<aside class="input-sideguide"/);
 assert.match(html,/입금통장표시에만 적용/);
 assert.match(html,/<div class="review-checklist"/);
 assert.match(html,/엑셀 파일 생성은 실제 송금이 아닙니다/);
 assert.match(html,/function validate\(/);
});

test('dispatch keeps live iframe, input focus and all existing business matchers',()=>{
 const html=pages.dispatch;
 assert.match(html,/iframe src="\.\/dispatch-tool\.html/);
 assert.match(html,/doc\.getElementById\('input'\)/);
 assert.match(html,/function createFuelMatcher\(/);
 assert.match(html,/function installCarrierReader\(/);
 assert.match(html,/탁송출발 주유비 미입력/);
 assert.match(html,/탁송기한/);
 assert.match(theme,/\.worktool-dispatch-frame #table/);
 assert.match(theme,/\.worktool-dispatch-frame #run/);
});

test('concept UI adapts to small screens and keyboard navigation',()=>{
 for(const marker of ['@media(max-width:760px)','@media(max-width:1050px)',':focus-visible']){
  assert.ok(theme.includes(marker),marker);
 }
 assert.match(theme,/\.concept-transfer \.input-grid\{grid-template-columns:minmax\(0,1fr\)\}/);
 assert.match(theme,/\.worktool-dispatch-frame \.workspace\{grid-template-columns:minmax\(0,1fr\)!important\}/);
 assert.ok(theme.includes('prefers-reduced-motion:reduce'));
});
