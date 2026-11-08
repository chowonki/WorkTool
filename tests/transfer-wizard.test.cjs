const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const root=path.resolve(__dirname,'..');
const read=name=>fs.readFileSync(path.join(root,name),'utf8');
const pages=['input','review','download'];
const core=read('transfer.html');
const wrapper=read('assets/transfer-wizard.js');
const styles=read('assets/concept-worktool.css');
const adapter=core.slice(core.indexOf('const TRANSFER_WORKFLOW_KEY='),core.lastIndexOf('</script>'));
const validRows=[{bank:'국민은행',account:'123456789012',amount:'35000',plate:'123가4567',outMemo:'123가4567',inMemo:'고객 환급금'}];
const draft=(rows=validRows)=>({rows:rows.map(x=>({...x})),selected:[],threads:['아직 전송하지 않은 원문'],outMemo:'',inMemo:'',cms:'',errorsOnly:false,splitSerial:0});

test('each transfer workflow stage is a separate functional HTML route',()=>{
 for(const [i,stage] of pages.entries()){
  const source=read('transfer-'+stage+'.html');
  assert.match(source,new RegExp('<body data-stage="'+stage+'">'));
  assert.match(source,new RegExp('src="\\.\\/transfer\\.html\\?step='+stage+'"'));
  for(const name of pages) assert.ok(source.includes('href="./transfer-'+name+'.html"'),stage+' -> '+name);
  assert.match(source,new RegExp('aria-current="step"'));
  assert.match(source,/assets\/transfer-wizard\.js/);
  assert.match(source,/id="workflowStatus"/);
  assert.match(source,/id="transferWorkflowFrame"/);
 }
 assert.match(read('index.html'),/href="\.\/transfer-input\.html"/);
 assert.match(read('quick-guide.html'),/href="\.\/transfer-input\.html"/);
 assert.ok(!read('assets/transfer-wizard.css').includes('display:none!important'));
});

test('hidden workflow views keep the shared bank validation and exporter alive',()=>{
 for(const stage of pages){
  assert.ok(styles.includes('data-workflow-step="'+stage+'"'));
 }
 for(const control of ['stepInput','stepReview','stepDownload','stepComments','download','parse','rows','plateDialog','missingPlateDialog']){
  assert.ok(core.includes('id="'+control+'"'),'original control missing '+control);
 }
 assert.match(adapter,/sessionStorage\.setItem\(TRANSFER_WORKFLOW_KEY/);
 assert.match(adapter,/sessionStorage\.removeItem\(TRANSFER_WORKFLOW_KEY/);
 assert.match(adapter,/restoreState\(payload\.state\)/);
 assert.match(wrapper,/child\.workflowSave\(\)/);
 assert.match(wrapper,/ResizeObserver/);
 assert.match(wrapper,/if\(event\.origin!==origin\|\|event\.source!==frame\.contentWindow\)return/);
});

function makeAdapter({step='review',state=draft(),storage=new Map(),now=10000}={}){
 const received=[];
 const mockSession={getItem:key=>storage.has(key)?storage.get(key):null,setItem:(key,value)=>storage.set(key,value),removeItem:key=>storage.delete(key)};
 const loc={search:'?step='+step,pathname:'/transfer.html',origin:'https://example.test'};
 const window={location:loc,sessionStorage:mockSession,parent:{postMessage:msg=>received.push(msg)},addEventListener(){}};
 let rows=state.rows.map(x=>({...x}));
 let restored=null,clearCalled=0,uiRefreshed=0;
 const notice={textContent:''};
 const document={documentElement:{classList:{add(){}},dataset:{}},addEventListener(){},querySelector(){return null}};
 const context=vm.createContext({
  window,document,URLSearchParams,
  Date:{now:()=>now},
  Number,JSON,Math,
  data:rows,
  MAX_TRANSFERS:100,
  undoState:null,lastDownloadState:null,commentSerial:0,
  validate:row=>row.amount?[]:['금액 확인'],
  amountSum:list=>list.reduce((n,row)=>n+Number(row.amount||0),0).toLocaleString('ko-KR'),
  snapshot:()=>draft(rows),
  restoreState:incoming=>{restored=incoming;rows=incoming.rows.map(x=>({...x}));context.data=rows;},
  refreshSelection:()=>uiRefreshed++,
  renderTransferComments:()=>uiRefreshed++,
  resetTransfer:()=>{clearCalled++;rows=[];context.data=rows;},
  $:()=>notice
 });
 vm.runInContext(adapter,context,{timeout:1000});
 return {context,storage,received,notice,rows:()=>rows,restored:()=>restored,uiRefreshed:()=>uiRefreshed,clearCalled:()=>clearCalled};
}

test('page navigation stores and restores bank fields and memo without changing bank validation',()=>{
 const storage=new Map();
 const first=makeAdapter({step:'input',storage});
 assert.equal(first.context.workflowSave(),true);
 const raw=storage.get('worktool.transfer.wizard.v1');
 assert.ok(raw);
 const saved=JSON.parse(raw);
 assert.equal(saved.state.rows[0].account,'123456789012');
 assert.equal(saved.state.rows[0].outMemo,'123가4567');
 assert.equal(saved.state.rows[0].inMemo,'고객 환급금');
 assert.equal(saved.state.threads[0],'아직 전송하지 않은 원문');
 const second=makeAdapter({step:'review',state:draft([]),storage});
 assert.equal(second.rows().length,1);
 assert.equal(second.rows()[0].inMemo,'고객 환급금');
 assert.equal(second.rows()[0].outMemo,'123가4567');
 assert.equal(second.context.workflowSummary().count,1);
 assert.equal(second.context.workflowSummary().invalid,0);
 assert.ok(second.uiRefreshed()>=2);
 assert.equal(second.context.workflowSave(),true);
 const third=makeAdapter({step:'download',state:draft([]),storage});
 assert.equal(third.context.workflowSummary().count,1);
 assert.equal(third.context.workflowSummary().total,'35,000');
});

test('expired and invalid sessions never restore silently',()=>{
 const storage=new Map([['worktool.transfer.wizard.v1',JSON.stringify({version:1,savedAt:1000,state:draft()})]]);
 const expired=makeAdapter({state:draft([]),storage,now:1000+4*60*60*1000+1});
 assert.equal(expired.rows().length,0);
 assert.equal(storage.size,0);
 storage.set('worktool.transfer.wizard.v1',JSON.stringify({version:1,savedAt:5000,state:{rows:Array.from({length:101},()=>validRows[0]),threads:[]}}));
 const invalid=makeAdapter({state:draft([]),storage,now:6000});
 assert.equal(invalid.rows().length,0);
 assert.equal(storage.size,0);
});

test('reset clears sensitive draft and restoration records from current session',()=>{
 const storage=new Map();
 const app=makeAdapter({step:'download',storage});
 assert.equal(app.context.workflowSave(),true);
 assert.equal(storage.size,1);
 assert.equal(app.context.workflowResetAll(),true);
 assert.equal(storage.size,0);
 assert.equal(app.rows().length,0);
 assert.equal(app.clearCalled(),1);
});

test('wizard navigation fails safely if browser storage is blocked',()=>{
 const x=makeAdapter();
 x.context.window.sessionStorage.setItem=()=>{throw Error('Storage blocked')};
 assert.equal(x.context.workflowSave(),false);
 assert.match(x.notice.textContent,/저장에 실패/);
});
