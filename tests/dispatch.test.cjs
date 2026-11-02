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
