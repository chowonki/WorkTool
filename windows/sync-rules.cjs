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
fs.writeFileSync(path.join(out,'transfer.js'),parser+'\n'+app.match(/const BANK_CODES=([\s\S]*?);/)[0]+String.raw`
const BANK_OPTIONS=Object.keys(BANK_CODES), LIMIT=100000000, MAX=100;
const PLATE_RE=/^(?:[가-힣]{2})?\d{2,3}[가-힣]\d{4}$/;
let rows=[], sequence=0;
const uid=()=>String(++sequence);
const won=n=>Number(n||0).toLocaleString('ko-KR');
function parseForReview(text) {
  const result = parseOne(text);
  const rawLines = text.split('\n');
  const accounts = result.items.map(item => item.account ? rawLines.findIndex(line => line.replace(/\D/g, '').includes(item.account)) : -1);
  const cuts = accounts.map(index => Math.max(0, index - 1));
  const labels = accounts.map((account, i) => {
    if (account < 0) return -1;
    for (let j = cuts[i] - 1; j >= 0 && j >= cuts[i] - 3; j--) {
      const line = rawLines[j].trim();
      if (line) return /^[(（][^()（）]{1,30}[)）]$/.test(line) ? j : -1;
    }
    return -1;
  });
  const start = i => labels[i] >= 0 ? labels[i] : cuts[i];
  result.items.forEach((item, i) => {
    const end = i + 1 < accounts.length && accounts[i + 1] >= 0 ? start(i + 1) : rawLines.length;
    item.ctx = accounts[i] >= 0 ? rawLines.slice(i === 0 ? 0 : start(i), end).join('\n') : item.source;
    item.label = labels[i] >= 0 ? rawLines[labels[i]].trim().slice(1, -1).trim() : '';
    item.holder = holderOf(item.ctx);
  });
  return result;
}
`+between('function check(r)','function counts()')+between('function splitRow(r)','const groupRows=')+
between('function amountText(','function renderComments(')+between('function fileName(stamp)','$(\'download\').onclick'));
console.log('Synced dispatch and transfer rules.');
