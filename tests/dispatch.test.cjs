const {test}=require('node:test');
const assert=require('node:assert/strict');
const {readFileSync}=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const base=readFileSync(path.join(__dirname,'../dispatch-tool.html'),'utf8');
const page=readFileSync(path.join(__dirname,'../dispatch.html'),'utf8');
function setup(choices=new Map(),confirmations=new Map()){
 const context=vm.createContext({readChoice:key=>choices.get(key),readOverride:key=>confirmations.get(key)});
 vm.runInContext(base.match(/<script id="core-source"[^>]*>([\s\S]*?)<\/script>/)[1],context);
 const reader=page.slice(page.indexOf('    function installCarrierReader('),page.indexOf('    if (core) {'));
 const matcher=page.slice(page.indexOf('function createFuelMatcher('),page.indexOf('    if (core && !core._latestAcrossVendors)'));
 vm.runInContext(reader+'\n'+matcher+'\ninstallCarrierReader(DispatchCore);DispatchCore.match=createFuelMatcher(DispatchCore,DispatchCore.match,readOverride,readChoice);',context);
 return context.DispatchCore;
}
function sheets(vendor,cost,state='탁송완료'){
 return [{name:'자료',rows:[['차량번호','상태',vendor==='A'?'주유/부대비용 총액':'주유비'],['123가4567',state,cost]]}];
}
const input=[{plate:'123가4567',date:''}];
test('Hanbit blanks become confirmed zero through reader and matcher',()=>{
 for(const value of ['',null,undefined,'   ',0]){
  const core=setup(),file=core.readSheets(sheets('A',value),'한빛.xlsx');
  assert.equal(file.records[0].cost,0);
  const row=core.match(input,[file])[0];
  assert.equal(row.amount,0);assert.equal(row.status,'confirmed');assert.equal(row.fuelState,'zero');
 }
});
test('matcher normalizes Hanbit blank records with null costs',()=>{
 const core=setup(),row=core.match(input,[{records:[{plate:'123가4567',vendor:'A',state:'완료',rawCost:'',cost:null}]}])[0];
 assert.equal(row.amount,0);assert.equal(row.fuelState,'zero');
});
test('Openmile blanks and malformed Hanbit costs require review',()=>{
 for(const [vendor,cost] of [['B',''],['A','확인필요']]){
  const core=setup(),row=core.match(input,[core.readSheets(sheets(vendor,cost),'업체.xlsx')])[0];
  assert.equal(row.amount,null);assert.equal(row.fuelState,'needs');
 }
});
test('blank costs retain completion and conflict checks',()=>{
 const core=setup();
 const pending=core.match(input,[core.readSheets(sheets('A','','배차완료'),'진행.xlsx')])[0];
 assert.equal(pending.status,'incomplete');assert.equal(pending.amount,null);
 const blank=core.readSheets(sheets('A',''),'빈칸.xlsx'),paid=core.readSheets(sheets('A',35000),'금액.xlsx');
 const row=core.match(input,[blank,paid])[0];
 assert.equal(row.status,'conflict');assert.equal(row.amount,null);assert.equal(row.fuelState,'needs');
});

function selectionHelpers(){
 const context=vm.createContext({queueMicrotask});
 const source=page.slice(page.indexOf('    function vendorSelectionRows('),page.indexOf('    function visibleRows()'));
 vm.runInContext(source,context);
 return context;
}
function dragHarness(){
 const helpers=selectionHelpers(),selected=new Set();let syncs=0;
 function surface(){const handlers={};return {handlers,addEventListener(type,fn){(handlers[type]||=[]).push(fn)},emit(type,event={}){for(const fn of handlers[type]||[])fn(event)}}}
 const win=surface(),doc=surface(),body=surface();doc.defaultView=win;
 let hit=null,captured=null;
 const boxes=['A','B','C','D'].map(plate=>({dataset:{plate},disabled:false,closest(selector){return selector==='.vehicle-select'?this:this.row}}));
 for(const box of boxes){box.row={querySelector:()=>box,closest(){return this}};}
 body.querySelectorAll=()=>boxes;body.contains=box=>boxes.includes(box);
 body.setPointerCapture=id=>captured=id;body.hasPointerCapture=id=>captured===id;body.releasePointerCapture=()=>captured=null;
 doc.elementFromPoint=()=>hit;
 const sync=()=>{syncs++;for(const box of boxes)box.checked=selected.has(box.dataset.plate)};
 helpers.installVehicleDragSelection(doc,body,selected,sync);
 const event=(extra={})=>({pointerId:1,button:0,buttons:1,isPrimary:true,preventDefault(){this.prevented=true},...extra});
 return {selected,boxes,body,doc,win,event,start(i,extra){const e=event({target:boxes[i],...extra});body.emit('pointerdown',e);return e},move(i,extra){hit=i===null?null:boxes[i].row;doc.emit('pointermove',event(extra))},stop(){doc.emit('pointerup',event())},syncs:()=>syncs};
}
test('drag selects skipped rows once, supports reverse movement and stops on release',()=>{
 const h=dragHarness();assert.equal(h.start(0).prevented,true);
 h.move(3);assert.deepEqual([...h.selected],['A','B','C','D']);
 h.move(1);h.move(3);assert.equal(h.selected.size,4);
 h.stop();h.move(0);assert.equal(h.selected.size,4);
 h.start(3);h.move(1);assert.deepEqual([...h.selected],['A']);
 h.stop();assert.equal(h.boxes[0].checked,true);assert.equal(h.boxes[1].checked,false);
});
test('drag cancellation, blur, disabled controls and keyboard clicks are safe',()=>{
 const h=dragHarness();h.start(0);h.move(null);assert.deepEqual([...h.selected],['A']);
 h.doc.emit('pointercancel',h.event());h.move(3);assert.equal(h.selected.size,1);
 h.start(1);h.win.emit('blur');h.move(3);assert.equal(h.selected.size,2);
 assert.equal(h.start(2,{button:2}).prevented,undefined);assert.equal(h.selected.has('C'),false);
 h.boxes[2].disabled=true;assert.equal(h.start(2).prevented,undefined);
 const keyboard=h.event({target:h.boxes[0],detail:0});h.body.emit('click',keyboard);assert.equal(keyboard.prevented,undefined);
 const pointer=h.event({target:h.boxes[0],detail:1});h.body.emit('click',pointer);assert.equal(pointer.prevented,true);
 h.start(3);h.move(0,{buttons:0});h.move(0);assert.equal(h.selected.has('A'),true);
});
test('vendor bulk selection matches current visible rows including shared vendors',()=>{
 const helpers=selectionHelpers(),rows=[{plate:'A',vendor:'한빛탁송'},{plate:'B',vendor:'오픈마일'},{plate:'C',vendor:'한빛탁송, 렌톡'},{plate:'D',vendor:''}];
 assert.deepEqual(Array.from(helpers.vendorSelectionRows(rows,'한빛탁송'),r=>r.plate),['A','C']);
 assert.deepEqual(Array.from(helpers.vendorSelectionRows(rows.slice(1),'한빛탁송'),r=>r.plate),['C']);
 const selected=new Set(['B']);
 for(const r of helpers.vendorSelectionRows(rows,'한빛탁송'))selected.add(r.plate);
 assert.deepEqual([...selected],['B','A','C']);
 for(const r of helpers.vendorSelectionRows(rows,'한빛탁송'))selected.delete(r.plate);
 assert.deepEqual([...selected],['B']);
 assert.equal(helpers.vendorSelectionRows(rows,'없는업체').length,0);
});

function rentokSheet(flag,memo='',state='완료',startRow=1){
 return [{name:'렌톡자료',startRow,rows:[
  ['출발지 차량번호','운행상태','출발지 주소(현재차량 상세위치)','출발시각','','','','','','캐리어탁송','메모'],
  ['123가4567',state,'서울','2026-11-03','','','','','',flag,memo]
 ]}];
}
test('Rentok J-column Y marks carrier and excludes inquiry regardless of completion',()=>{
 for(const flag of ['Y',' y ','Ｙ'])for(const state of ['완료','진행중']){
  const core=setup(),file=core.readSheets(rentokSheet(flag,'',state,5),'렌톡.xlsx');
  assert.equal(file.records[0].carrier,true);
  const row=core.match(input,[file])[0];
  assert.equal(row.status,'carrier');assert.equal(row.carrier,true);assert.equal(row.cInquiry,false);
  assert.equal(row.amount,null);assert.equal(row.fuelState,'exempt');
  assert.match(row.carrierMemo,/캐리어탁송\(J열\): Y/);
 }
});
test('Rentok N and blank flags are not carriers even when memo mentions carrier',()=>{
 for(const flag of ['N','',null,'YES',1]){
  const core=setup(),file=core.readSheets(rentokSheet(flag,'캐리어 문의'),'렌톡.xlsx');
  assert.equal(file.records[0].carrier,false);
  const row=core.match(input,[file])[0];
  assert.equal(row.carrier,false);assert.equal(row.cInquiry,true);assert.notEqual(row.status,'carrier');
 }
});
test('carrier flag changes invalidate review identity and remain sheet-specific',()=>{
 const core=setup(),yes=core.readSheets(rentokSheet('Y'),'렌톡.xlsx'),no=core.readSheets(rentokSheet('N'),'렌톡.xlsx');
 assert.notEqual(core.match(input,[yes])[0].reviewKey,core.match(input,[no])[0].reviewKey);
 const sheets=[...rentokSheet('Y'),...rentokSheet('N').map(s=>({...s,name:'일반운행',rows:s.rows.map((r,i)=>i?r.map(v=>v==='123가4567'?'45나6789':v):r)}))];
 const records=core.readSheets(sheets,'렌톡.xlsx').records;
 assert.equal(records.find(r=>r.plate==='123가4567').carrier,true);
 assert.equal(records.find(r=>r.plate==='45나6789').carrier,false);
});

function trip(vendor,amount,date='2026-11-03',extra={}){
 return {vendor,plate:'123가4567',dispatchDate:date,state:'완료',cost:amount,rawCost:amount,origin:'서울',file:vendor+'.xlsx',sheet:'자료',row:2,...extra};
}
test('user can resolve conflicting trips and exports contain the selected record',()=>{
 const choices=new Map(),core=setup(choices),files=[{records:[trip('A',35000),trip('B',50000)]}];
 let row=core.match(input,files)[0];assert.equal(row.status,'conflict');assert.equal(row.recordChoices.length,2);
 choices.set(row.choiceKey,row.recordChoices.find(c=>c.record.vendor==='A').id);
 row=core.match(input,files)[0];assert.equal(row.status,'confirmed');assert.equal(row.amount,35000);assert.equal(row.vendor,'한빛탁송');assert.equal(row.latestConflict,false);
 assert.match(row.selectionReason,/사용자/);assert.equal(row.sources,'A.xlsx / 자료 / 2행');
 assert.equal(core.values(row)[2],35000);assert.match(core.textExport([row]),/35000/);assert.doesNotMatch(core.textExport([row]),/50000/);
 choices.delete(row.choiceKey);assert.equal(core.match(input,files)[0].status,'conflict');
});
test('explicit trip selection can apply a different date and is shared per vehicle',()=>{
 const choices=new Map(),core=setup(choices),files=[{records:[trip('A',12345,'2026-11-01'),trip('B',50000)]}];
 const dated=[{plate:'123가4567',date:'2026-11-03 10:00'}];let row=core.match(dated,files)[0];
 assert.equal(row.amount,50000);choices.set(row.choiceKey,row.recordChoices.find(c=>c.record.vendor==='A').id);
 row=core.match(dated,files)[0];assert.equal(row.dispatchDate,'2026-11-01');assert.equal(row.targetDate,'2026-11-01');assert.equal(row.amount,12350);
 assert.ok(row.fuelNotes.some(n=>n.includes('다른 운행')));
 const rows=core.match([...dated,{plate:'123가4567',date:'2026-11-01'}],files);
 assert.ok(rows.every(r=>r.amount===12350&&r.status==='confirmed'));
});
test('selected pending, blank cost and Rentok carrier flags retain their own states',()=>{
 const choices=new Map(),core=setup(choices),files=[{records:[trip('A',35000),trip('B',10000,'2026-11-03',{state:'진행중'}),trip('B',null,'2026-11-02',{rawCost:''}),trip('C',null,'2026-11-01',{carrier:true,carrierFlag:'Y'}),trip('C',null,'2026-11-02',{carrier:false,carrierFlag:'N'})]}];
 let row=core.match(input,files)[0];
 for(const [index,status,fuel,amount,inquiry] of [[0,'confirmed','verified',35000,false],[1,'incomplete','waiting',null,false],[2,'cost','needs',null,false],[3,'carrier','exempt',null,false],[4,'slack','inquiry',null,true]]){
  choices.set(row.choiceKey,row.recordChoices[index].id);row=core.match(input,files)[0];
  assert.equal(row.status,status);assert.equal(row.fuelState,fuel);assert.equal(row.amount,amount);assert.equal(row.cInquiry,inquiry);
 }
});
test('record changes discard old trip choices and identical duplicates share one option',()=>{
 const choices=new Map(),core=setup(choices),a=trip('A',35000),b=trip('B',50000),files=[{records:[a,b,{...a,file:'재다운로드.xlsx'}]}];
 let row=core.match(input,files)[0];assert.equal(row.recordChoices.length,2);assert.equal(row.duplicateRecords,1);
 choices.set(row.choiceKey,row.recordChoices.find(c=>c.record.vendor==='A').id);
 assert.equal(core.match(input,files)[0].amount,35000);
 b.cost=60000;b.rawCost=60000;row=core.match(input,files)[0];assert.equal(row.selectedRecordId,'');assert.equal(row.status,'conflict');
});
test('manual fuel confirmations are isolated for each chosen trip',()=>{
 const choices=new Map(),confirmations=new Map(),core=setup(choices,confirmations),files=[{records:[trip('A',35000),trip('B',50000)]}];
 let row=core.match(input,files)[0];choices.set(row.choiceKey,row.recordChoices[0].id);row=core.match(input,files)[0];
 confirmations.set(row.reviewKey,{amount:36000,reason:'업체 확인',savedAt:'오늘'});
 assert.equal(core.match(input,files)[0].amount,36000);
 choices.set(row.choiceKey,row.recordChoices[1].id);row=core.match(input,files)[0];assert.equal(row.amount,50000);assert.equal(row.manualConfirmation,undefined);
 choices.set(row.choiceKey,row.recordChoices[0].id);assert.equal(core.match(input,files)[0].amount,36000);
});
