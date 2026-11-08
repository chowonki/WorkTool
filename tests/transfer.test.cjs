const {test}=require('node:test');
const assert=require('node:assert/strict');
const {readFileSync}=require('node:fs');
const vm=require('node:vm');
const html=readFileSync(require('node:path').join(__dirname,'../transfer.html'),'utf8');
// Run the page's actual parsing and validation code without browser event bindings.
const source=html.match(/<script>([\s\S]*?)<\/script>/)[1].split("$('errorsOnly').onchange=")[0];
const context=vm.createContext({});
vm.runInContext(source,context);
const run=code=>vm.runInContext(code,context,{timeout:1000});

test('documented whole-won formats preserve their exact value',()=>{
 for(const [text,want] of [['35,000원','35000'],['1000만원','10000000'],['천만원','10000000'],['1억 2천만원','120000000'],['금액: 10000000','10000000'],['₩35,000','35000'],['금액: 7,240,000원','7240000']]){
  assert.equal(context.parseKoreanAmount(text),want,text);
 }
});
test('signed, decimal and malformed amounts never become positive transfers',()=>{
 for(const value of ['-35000원','- 35000원','−35,000원','금액: -35000원','금액 -35000원','금액: -천만원','₩-35000','1.5만원','.5만원','35,000.50원','35,00원','35,00원 500원','1/2만원','1e5원','35000원 / 40000원']){
  assert.equal(context.parseKoreanAmount(value),'',value);
  const thread=`국민은행\n123-456-789012\n${value}`;
  assert.equal(context.parseOne(thread).items[0].amount,'',thread);
 }
});
test('manual edits use the same amount checks as extraction',()=>{
 for(const value of ['-35000','1.5만원','35,00원','35,00','₩-35000']){
  const amount=context.editedAmount(value);
  assert.ok(context.validate({bank:'국민은행',account:'123456789012',amount}).includes('금액 확인'),value);
 }
 for(const value of ['35000','35,000','35,000원','3만 5천원'])assert.equal(context.editedAmount(value),'35000');
});
test('bank validation accepts known aliases and rejects arbitrary names',()=>{
 for(const [bank,want] of [['KB','국민은행'],['국민은행','국민은행'],[' keb하나 ','하나은행'],['NH','농협은행'],['iM뱅크','iM뱅크']]){
  assert.equal(context.canonicalBank(bank),want);
  assert.ok(!context.validate({bank,account:'123456789012',amount:'35000'}).includes('은행명 확인'));
 }
 for(const bank of ['', '없는은행','국민은행오타'])assert.ok(context.validate({bank,account:'123456789012',amount:'35000'}).includes('은행명 확인'));
});
test('splits preserve totals and stop before allocating more than 100 rows',()=>{
 for(const amount of ['100000000','100000001','120000000','10000000000']){
  const rows=context.expandTransfer({amount});
  assert.ok(rows.length<=100);
  assert.equal(rows.reduce((sum,r)=>sum+BigInt(r.amount),0n),BigInt(amount));
  assert.ok(rows.every(r=>Number(r.amount)<=100000000));
 }
 for(const amount of ['10000000001','9007199254740991'])assert.throws(()=>run(`expandTransfer({amount:'${amount}'})`),/100건/);
 assert.throws(()=>run("expandTransfers([{amount:'10000000000'},{amount:'1'}])"),/100건/);
});
test('automatic split duplicates are allowed while independent duplicates are blocked',()=>{
 run("data=expandTransfer({bank:'국민은행',account:'123456789012',amount:'200000000',plate:''})");
 assert.equal(run('data.every(row=>validate(row).length===0)'),true);
 run("data.push({...data[0],splitGroup:''})");
 assert.equal(run("validate(data[2]).includes('동일 계좌·금액·차량번호 중복')"),true);
 run('data=[]');
});

const filenameSource=html.match(/<script>([\s\S]*?)<\/script>/)[1].split("let lastExportStamp=")[1].split('function excel()')[0];
function filenameContext(){
 const fresh=vm.createContext({});
 vm.runInContext(source+"\nlet lastExportStamp="+filenameSource,fresh);
 return fresh;
}
test('download names identify the exported vehicles and exact total without extra input',()=>{
 const now=new Date('2026-10-31T05:20:35Z');
 const cases=[
  [[{plate:'123가4567',amount:'35000'}],'123가4567_35,000원_1031_142035.xlsx'],
  [[{plate:'123가4567',amount:'50000'},{plate:'45나6789',amount:'400000'},{plate:'234다5678',amount:'400000'}],'123가4567외2대_총850,000원_1031_142035.xlsx'],
  [[{amount:'50000'},{amount:'400000'},{amount:'400000'}],'송금3건_총850,000원_1031_142035.xlsx'],
  [[{plate:'123가4567',amount:'100000000'},{plate:'123가4567',amount:'20000000'}],'123가4567_총120,000,000원_1031_142035.xlsx'],
  [[{plate:'123가4567',commentPlates:['123가4567','45나6789','234다5678'],amount:'850000'}],'123가4567외2대_총850,000원_1031_142035.xlsx'],
  [[{plate:'45나6789',commentPlates:['123가4567'],amount:'35000'}],'45나6789_35,000원_1031_142035.xlsx']
 ];
 for(const [rows,want] of cases)assert.equal(filenameContext().uniqueTransferFilename(rows,now),want);
});
test('same-second exports get a suffix and dates use Korean time across midnight',()=>{
 const fresh=filenameContext(),rows=[{plate:'123가4567',amount:'35000'}],now=new Date('2026-10-31T15:00:00Z');
 assert.equal(fresh.uniqueTransferFilename(rows,now),'123가4567_35,000원_1101_000000.xlsx');
 assert.equal(fresh.uniqueTransferFilename(rows,now),'123가4567_35,000원_1101_000000_2.xlsx');
 assert.equal(fresh.uniqueTransferFilename(rows,now),'123가4567_35,000원_1101_000000_3.xlsx');
 assert.equal(fresh.uniqueTransferFilename(rows,new Date('2026-10-31T15:00:01Z')),'123가4567_35,000원_1101_000001.xlsx');
});


test('comments use manwon only for exact multiples without changing export amounts',()=>{
 for(const [amount,want] of [['1000000','100만원'],['10000','1만원'],['100000','10만원'],['10000000','1,000만원'],['35000','35,000원'],['7240000','724만원'],['120000000','12,000만원'],['12345','12,345원'],['9999','9,999원'],['100000001','100,000,001원']]){
  const row={amount,commentTarget:'기타계좌'};
  const result=context.transferComment([row]);
  assert.equal(result.text,`금액 ${want} 기타계좌로 송금완료`);
  assert.equal(result.ready,true);
  assert.equal(row.amount,amount);
 }
 const rows=context.expandTransfer({amount:'120000000',commentTarget:'딜러에게',commentItem:'same-payment'});
 assert.equal(context.transferComment(rows).text,'차대금 12,000만원 딜러에게 송금완료 (총액 12,000만원)');
 assert.equal(rows.reduce((sum,r)=>sum+BigInt(r.amount),0n),120000000n);
 assert.equal(context.transferComment([{amount:'',commentTarget:'고객계좌'}]).ready,false);
 assert.equal(context.transferComment([{amount:'',commentTarget:'고객계좌'}]).text,'금액 금액 확인 필요 고객계좌로 송금완료');
});

test('dealer comments use vehicle payment wording per recipient',()=>{
 const row={amount:'1000000',commentTarget:'딜러에게'};
 assert.equal(context.transferComment([row]).text,'차대금 100만원 딜러에게 송금완료');
 assert.equal(context.transferComment([row]).ready,true);
 assert.equal(context.transferComment([row,{amount:'35000',commentTarget:'고객계좌'}]).text,'차대금 100만원 딜러에게, 금액 35,000원 고객계좌로 송금완료 (총액 1,035,000원)');
 assert.equal(context.transferComment([{amount:'',commentTarget:'딜러에게'}]).ready,false);
});

test('mortgage and customer comments use manwon only for exact multiples',()=>{
 for(const target of ['저당계좌','저당송금','저당 상환계좌','고객계좌','고객','고객에게']){
  for(const [amount,want] of [['1000000','100만원'],['35000','35,000원'],['12345','12,345원'],['9999','9,999원'],['100000001','100,000,001원']]){
   const result=context.transferComment([{amount,commentTarget:target}]);
   assert.ok(result.text.includes(`금액 ${want} ${target}`),result.text);
   assert.equal(result.ready,true);
  }
  const rows=context.expandTransfer({amount:'120000001',commentTarget:target});
  assert.ok(context.transferComment(rows).text.includes('금액 120,000,001원 '));
  assert.equal(rows.reduce((sum,r)=>sum+BigInt(r.amount),0n),120000001n);
 }
});

test('extracted mortgage and customer labels use full won amounts in mixed comments',()=>{
 const parsed=context.parseOne('국민은행 (저당계좌)\n123-456-789012\n100만원\n신한은행 (고객계좌)\n110-123-456789\n35,000원');
 assert.equal(context.transferComment(parsed.items).text,'금액 100만원 저당계좌, 금액 35,000원 고객계좌로 송금완료 (총액 1,035,000원)');
 assert.equal(context.transferComment(parsed.items).ready,true);
});


test('all recipients use full won for remainders and aggregate splits before formatting',()=>{
 for(const target of ['저당계좌','고객계좌','딜러에게','기타계좌']){
  for(const [amount,want] of [['1000000','100만원'],['1005000','1,005,000원'],['10001','10,001원'],['10000','1만원'],['1','1원'],['120000000','12,000만원'],['120000001','120,000,001원']]){
   const rows=context.expandTransfer({amount,commentTarget:target});
   assert.ok(context.transferComment(rows).text.includes(want+' '+target));
   assert.equal(rows.reduce((sum,r)=>sum+BigInt(r.amount),0n),BigInt(amount));
  }
 }
});

test('multiple transfers include the exact total without counting automatic splits twice',()=>{
 const split=context.expandTransfer({amount:'120000001',commentTarget:'저당계좌'});
 assert.equal(context.transferComment(split).text,'금액 120,000,001원 저당계좌로 송금완료 (총액 120,000,001원)');
 assert.equal(context.transferComment([...split,{amount:'9999',commentTarget:'고객계좌'}]).text,'금액 120,000,001원 저당계좌, 금액 9,999원 고객계좌로 송금완료 (총액 12,001만원)');
 assert.equal(context.transferComment([{amount:'1000000',commentTarget:'저당계좌'},{amount:'2000000',commentTarget:'고객계좌'}]).text,'금액 100만원 저당계좌, 금액 200만원 고객계좌로 송금완료 (총액 300만원)');
 const invalid=context.transferComment([{amount:'',commentTarget:'저당계좌'},{amount:'1000000',commentTarget:'고객계좌'}]);
 assert.equal(invalid.ready,false);
 assert.ok(invalid.text.endsWith('(총액 금액 확인 필요)'));
 assert.equal(context.transferComment([{amount:'1000000',commentTarget:'고객계좌'}]).text,'금액 100만원 고객계좌로 송금완료');
});

test('Excel XML removes forbidden control characters and preserves valid text',()=>{
 const value='  앞쪽'+String.fromCharCode(0,1)+'중간'+String.fromCharCode(11,31)+'끝'+String.fromCharCode(65534,65535)+' & < > " \'  ';
 const helper=html.match(/const xml=s=>[\s\S]*?;const crcTable=/)?.[0];
 assert.ok(helper,'실제 Excel XML 이스케이프 함수가 존재해야 합니다.');
 vm.runInContext(helper.replace(/;const crcTable=$/,''),context);
 const actual=run('xml('+JSON.stringify(value)+')');
 assert.equal(actual,'  앞쪽중간끝 &amp; &lt; &gt; &quot; &apos;  ');
 assert.equal(run('xml('+JSON.stringify('A\\tB\\nC\\rD')+')'),'A\\tB\\nC\\rD');
 assert.match(html,/xml:space="preserve"/);
});

test('duplicate check canonicalizes bank aliases, account separators and plate whitespace',()=>{
 run("data=[{bank:'KB',account:'123-456-789012',amount:'35000',plate:'123가4567'},{bank:'국민은행',account:'123456789012',amount:'35000',plate:'123가4567'}]");
 assert.ok(run('validate(data[0])').includes('동일 계좌·금액·차량번호 중복'));
 assert.ok(run('validate(data[1])').includes('동일 계좌·금액·차량번호 중복'));
 run("data[1].splitGroup='split-test';data[0].splitGroup='split-test'");
 assert.ok(!run('validate(data[0])').includes('동일 계좌·금액·차량번호 중복'));
 run("data[1].splitGroup='';data[1].amount='35001'");
 assert.ok(!run('validate(data[0])').includes('동일 계좌·금액·차량번호 중복'));
 run('data=[]');
});
