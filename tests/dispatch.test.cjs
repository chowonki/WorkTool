const {test}=require('node:test');
const assert=require('node:assert/strict');
const {readFileSync}=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const base=readFileSync(path.join(__dirname,'../dispatch-tool.html'),'utf8');
const page=readFileSync(path.join(__dirname,'../dispatch.html'),'utf8');
function setup(){
 const context=vm.createContext({});
 vm.runInContext(base.match(/<script id="core-source"[^>]*>([\s\S]*?)<\/script>/)[1],context);
 const reader=page.slice(page.indexOf('    function installCarrierReader('),page.indexOf('    if (core) {'));
 const matcher=page.slice(page.indexOf('function createFuelMatcher('),page.indexOf('    if (core && !core._latestAcrossVendors)'));
 vm.runInContext(reader+'\n'+matcher+'\ninstallCarrierReader(DispatchCore);DispatchCore.match=createFuelMatcher(DispatchCore,DispatchCore.match);',context);
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
