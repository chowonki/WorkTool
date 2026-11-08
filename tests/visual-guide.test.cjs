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
 assert.match(guide,/이체파일은 송금 예약이나 실행이 아닙니다/);
});
test('screen guides use real PNG assets with Korean-readable screenshots',()=>{
 for(const filename of ['dispatch-screen.png','transfer-screen.png','transfer-Review.png','transfer-Download.png']){
  const name='guide/'+filename;
  const contents=fs.readFileSync(path.join(root,name));
  assert.ok(contents.byteLength>20000,name+' must not be empty/blank');
  assert.equal(contents.subarray(0,8).toString('hex'),'89504e470d0a1a0a',name+' must be PNG');
  assert.ok(guide.includes('./'+name),name+' must appear in the quick guide');
 }
});
test('the guide uses expandable screenshots and keeps actual tool navigation',()=>{
 assert.match(guide,/<img src="\.\/guide\/dispatch-screen\.png"/);
 assert.match(guide,/<img src="\.\/guide\/transfer-screen\.png"/);
 assert.match(guide,/<img src="\.\/guide\/transfer-Review\.png"/);
 assert.match(guide,/<img src="\.\/guide\/transfer-Download\.png"/);
 assert.match(guide,/href="\.\/dispatch\.html"/);
 assert.match(guide,/href="\.\/transfer\.html"/);
 assert.match(guide,/target="_blank" rel="noopener"/);
});
