// Copy only browser-independent business rules. Run after changing web rules.
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..'),out=path.join(__dirname,'WorkTool.Core/Rules');
fs.mkdirSync(out,{recursive:true});
const dispatch=fs.readFileSync(path.join(root,'dispatch.html'),'utf8');
const transfer=fs.readFileSync(path.join(root,'transfer.html'),'utf8');
fs.writeFileSync(path.join(out,'dispatch.js'),dispatch.match(/<script id="core-source"[^>]*>([\s\S]*?)<\/script>/)[1]);
const scripts=[...transfer.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]);
let parser=scripts.find(s=>s.includes('function parseOne('));
parser=parser.slice(0,parser.indexOf('// Minimal offline OOXML'));
const app=scripts.find(s=>s.includes('function check(r)'));
const between=(a,b)=>app.slice(app.indexOf(a),app.indexOf(b));
fs.writeFileSync(path.join(out,'transfer.js'),parser+'\n'+app.match(/const BANK_CODES=([\s\S]*?);/)[0]+`
const BANK_OPTIONS=Object.keys(BANK_CODES), LIMIT=100000000, MAX=100;
const PLATE_RE=/^(?:[가-힣]{2})?\\d{2,3}[가-힣]\\d{4}$/;
let rows=[], sequence=0;
const uid=()=>String(++sequence);
const won=n=>Number(n||0).toLocaleString('ko-KR');
`+between('function check(r)','function counts()')+between('function splitRow(r)','const groupRows=')+between('function amountText(','function renderComments('));
console.log('Synced dispatch and transfer rules.');
