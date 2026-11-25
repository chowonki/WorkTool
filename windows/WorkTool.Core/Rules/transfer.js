
/* 이전 버전(hana_transfer_append_rows)의 추출 규칙과 엑셀 작성기를 그대로 사용 */
const banks=[
 ['하나은행','하나'],['KEB하나','하나'],['하나','하나'],['국민은행','국민'],['KB국민','국민'],['국민','국민'],['신한은행','신한'],['신한','신한'],['우리은행','우리'],['우리','우리'],['농협은행','농협'],['지역농협','농협'],['단위농협','농협'],['농축협','농협'],['NH농협','농협'],['농협','농협'],['기업은행','기업'],['IBK기업','기업'],['기업','기업'],['산업은행','산업'],['KDB','산업'],['산업','산업'],['수협은행','수협'],['수협','수협'],['SC제일','SC제일'],['제일은행','SC제일'],['SC은행','SC제일'],['한국씨티','한국씨티'],['씨티','한국씨티'],['시티','한국씨티'],
 ['iM뱅크','iM뱅크(대구)'],['아이엠뱅크','iM뱅크(대구)'],['대구은행','iM뱅크(대구)'],['대구','iM뱅크(대구)'],['DGB','iM뱅크(대구)'],['부산은행','부산'],['BNK부산','부산'],['부산','부산'],['경남은행','경남'],['BNK경남','경남'],['경남','경남'],['광주은행','광주'],['광주','광주'],['전북은행','전북'],['전북','전북'],['제주은행','제주'],['제주','제주'],
 ['카카오뱅크','카카오뱅크'],['카뱅','카카오뱅크'],['케이뱅크','케이뱅크'],['K뱅크','케이뱅크'],['토스뱅크','토스뱅크'],
 ['새마을금고','새마을금고'],['MG새마을','새마을금고'],['새마을','새마을금고'],['신협','신협'],['우체국','우체국'],['저축은행','저축'],['저축','저축'],['산림조합','산림조합'],
 ['도이치','도이치뱅크'],['중국건설','중국건설'],['중국공상','중국공상'],['중국은행','중국'],['BNP','BNP파리바'],['뱅크오브아메리카','BOA'],['BOA','BOA'],['HSBC','HSBC'],['JP모간','JP모간'],['JP모건','JP모간'],
 ['교보증권','교보'],['다올투자','다올투자'],['다올','다올투자'],['대신증권','대신'],['메리츠증권','메리츠'],['메리츠','메리츠'],['미래에셋','미래에셋'],['부국증권','부국'],['삼성증권','삼성'],['상상인증권','상상인'],['상상인','상상인'],['신영증권','신영'],['신한투자','신한투자'],['신한금융투자','신한투자'],['아이엠증권','아이엠'],['iM증권','아이엠'],['하이투자','아이엠'],['우리투자','우리투자'],['유안타','유안타'],['유진투자','유진'],['유진증권','유진'],['카카오페이증권','카카오페이'],['케이프투자','케이프'],['케이프','케이프'],['키움','키움'],['토스증권','토스'],['하나증권','하나증권'],['하나금융투자','하나증권'],['한국투자','한국투자'],['한투','한국투자'],['한화투자','한화투자'],['현대차증권','현대차'],['BNK투자','BNK투자'],['DB금융투자','DB'],['DB투자','DB'],['IBK투자','IBK투자'],['KB증권','KB'],['LS증권','LS'],['이베스트','LS'],['NH투자','NH투자'],['SK증권','SK']];
const MAX_TRANSFERS=100;let data=[];const $=id=>document.getElementById(id);const norm=s=>String(s||'').replace(/\s/g,'');const bankOf=s=>{let z=norm(s).toLowerCase();return [...banks].sort((a,b)=>b[0].length-a[0].length).find(x=>z.includes(x[0].toLowerCase()))?.[1]||''};const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function findPlate(text){let decoded=text;try{decoded=decodeURIComponent(text)}catch(e){}const matches=[...decoded.matchAll(/(?:^|[^0-9가-힣])((?:\d{2,3}\s*[가-힣]\s*\d{4})|(?:[가-힣]{2}\s*\d{2}\s*[가-힣]\s*\d{4}))(?![0-9가-힣])/g)].map(m=>m[1].replace(/\s/g,''));return [...new Set(matches)].length===1?matches[0]:''}
// Korean amount parser. Returns decimal KRW string, or empty on missing/ambiguous values.
const koDigits={'영':0,'공':0,'일':1,'이':2,'삼':3,'사':4,'오':5,'육':6,'칠':7,'팔':8,'구':9};
const koSmall={'십':10,'백':100,'천':1000};
function parseKoreanInteger(token){
 token=String(token).replace(/[\s,]/g,'');if(!token)return null;
 if(/^\d+$/.test(token))return Number(token);
 let sum=0,pending='',seen=false;
 for(const ch of token){
  if(/\d/.test(ch)){pending+=ch;seen=true;continue}
  if(ch in koDigits){pending+=String(koDigits[ch]);seen=true;continue}
  if(ch in koSmall){sum+=(pending?Number(pending):1)*koSmall[ch];pending='';seen=true;continue}
  return null;
 }
 return seen?sum+(pending?Number(pending):0):null;
}
function parseKoreanAmount(input){
 const rawText=String(input||'').replace(/\u00a0/g,' ').normalize('NFKC');
 // 잘못 읽으면 엉뚱한 금액이 되는 표기는 금액으로 만들지 않고 '금액 확인'으로 남겨요.
 // 3.5억·35,000.50원(소수), -35,000원(음수), 1/2만원(분수), 1e5원(지수), 35,00원(쉼표 위치 오류)
 if(/\.\s*\d+\s*(?:[십백천]?\s*[만억]|원)/.test(rawText))return '';
 if(/(?:^|[^\d])[-−–]₩?\d|^\s*[-−–]\s*₩?\s*\d|[-−–]\s*[0-9영공일이삼사오육칠팔구십백천]+\s*[만억]/.test(rawText))return '';
 if(/\d\s*\/\s*\d|\d[eE][+-]?\d/.test(rawText))return '';
 if((rawText.match(/\d[\d,]*\d/g)||[]).some(n=>n.includes(',')&&!/^\d{1,3}(?:,\d{3})+$/.test(n)))return '';
 const s=rawText.replace(/,/g,'');
 // Match adjacent segments such as 1억 2천 5백만원, 천만원, 3만5천원.
 const units='[0-9영공일이삼사오육칠팔구십백천]+';
 const re=new RegExp('('+units+'\\s*억(?:\\s*'+units+'\\s*만)?(?:\\s*'+units+'\\s*원?)?|'+units+'\\s*만(?:\\s*'+units+'\\s*원?)?|'+units+'\\s*원)','g');
 let matches=[...s.matchAll(re)].map(x=>x[1]);
 // Pure digit amounts only if explicitly marked as KRW, or a short, standalone numeric line.
 if(!matches.length){
  const money=s.match(/(?:금액|입금금액|송금액|이체금액|지급액|비용|주유비|총액|합계)\s*[:：]?\s*(?:₩\s*)?(\d{1,12})(?![\d-])/);
  if(money)return String(Number(money[1]));
  const plain=s.trim().match(/^(?:₩\s*)?(\d{1,9})$/);
  return plain?String(Number(plain[1])):'';
 }
 if(matches.length!==1)return '';
 let token=matches[0].replace(/\s/g,'').replace(/원$/,'');let total=0;
 for(const [unit,factor] of [['억',1e8],['만',1e4]]){
  if(token.includes(unit)){const parts=token.split(unit);if(parts.length!==2)return '';const n=parseKoreanInteger(parts[0]);if(n===null)return '';total+=n*factor;token=parts[1]}
 }
 if(token){const rest=parseKoreanInteger(token);if(rest===null)return '';total+=rest}
 return Number.isSafeInteger(total)&&total>0?String(total):'';
}
// 2026-11-17, 2026.11.17 14:30 같은 날짜·시간 줄은 계좌번호로 보지 않아요.
function isDateLine(s){return /^[^\d]*(?:19|20)\d{2}\s*[-./년]\s*\d{1,2}\s*[-./월]\s*\d{1,2}\s*일?\s*(?:\(?[월화수목금토일]\)?)?\s*(?:(?:오전|오후)?\s*\d{1,2}:\d{2}(?::\d{2})?)?[^\d]*$/.test(String(s).trim());}
// 목록 기호("- ", "•")만 지우고, 붙어 있는 마이너스(-35000원)는 남겨서 금액 확인으로 보내요.
function cleanLine(s){return s.replace(/^\s*>\s*/, '').replace(/^\s*(?:[-–]\s+|•\s*)/, '').trim()}
// 예금주·받는 분 줄에 은행 이름이 들어 있어도(예: "예금주 : 배효정웰컴저축은행") 은행 줄로 보지 않아요.
// 예금주 칸용: 스레드의 "예금주 : ..." 값을 그대로 가져와요. 없어도 오류로 보지 않아요.
function holderOf(text){const m=String(text||'').match(/^[\s>•-]*예금주(?:명)?\s*[:：]?\s*(.+)$/m);return m?m[1].trim():'';}
function isDepositor(s){return /^(예금주|예금주명|받는\s*분|받는분|수취인|성명|이름)\s*[:：]?/.test(String(s).trim());}
function parseOne(rawInput){const raw=rawInput.replace(/\r/g,'');
 const lines=raw.split('\n').map(cleanLine).filter(Boolean);
 const threadPlates=[...new Set(lines.filter(l=>!/https?:\/\/|www\./i.test(l)).flatMap(l=>{const decoded=l;return [...decoded.matchAll(/(?:^|[^0-9가-힣])((?:\d{2,3}\s*[가-힣]\s*\d{4})|(?:[가-힣]{2}\s*\d{2}\s*[가-힣]\s*\d{4}))(?![0-9가-힣])/g)].map(m=>m[1].replace(/\s/g,''))}))];
 const sharedPlate=threadPlates.length===1?threadPlates[0]:'';
 const isUrl=s=>/https?:\/\/|www\.|slack\.com\/archives\//i.test(s);
 const getAccount=s=>{if(isUrl(s)||findPlate(s)||isDateLine(s)||/(?:원|만원|금액|송금액|이체금액|주유비|지급액|₩)/.test(s))return '';const m=s.match(/(?:계좌(?:번호)?\s*[:：]?\s*)?((?:\d[\d\s-]{6,}\d))/);return m?m[1].replace(/\D/g,''):''};
 const getAmount=s=>{if(isUrl(s)||getAccount(s)||findPlate(s))return '';return parseKoreanAmount(s)};
 const isBank=s=>!isUrl(s)&&!isDepositor(s)&&!!bankOf(s)&&s.length<35;
 let items=[],prevEnd=0;
 for(let i=0;i<lines.length;i++){
  if(!isBank(lines[i]))continue;
  const bank=bankOf(lines[i]);
  let ai=-1;for(let j=i+1;j<Math.min(lines.length,i+5);j++){if(isBank(lines[j]))break;if(getAccount(lines[j])){ai=j;break}}
  if(ai<0)continue;
  let mi=-1;
  for(let j=ai+1;j<Math.min(lines.length,ai+7);j++){
   if(isBank(lines[j]))break;
   if(mi<0&&getAmount(lines[j])){mi=j;break}
  }
  const prev=lines.slice(Math.max(prevEnd,i-12),i);
  const plateCandidates=prev.filter(l=>!isUrl(l)).flatMap(l=>[findPlate(l)]).filter(Boolean);
  const plate=[...new Set(plateCandidates)].length===1?plateCandidates[0]:'';
  items.push({bank,account:getAccount(lines[ai]),amount:mi>=0?getAmount(lines[mi]):'',plate,source:lines.slice(Math.max(prevEnd,i-3),Math.max(mi,ai)+1).join('\n')});
  prevEnd=Math.max(mi,ai)+1;i=Math.max(i,prevEnd-1);
 }
 if(!items.length){items=raw.split(/\n\s*\n+/).map(x=>x.trim()).filter(Boolean).map(parseBlock)}
 items=items.filter(x=>x.bank||x.account||x.amount||x.plate);if(sharedPlate)items.forEach(x=>x.plate=sharedPlate);return {items,ambiguous:threadPlates.length>1};
}
function parseBlock(b){const lines=b.split('\n').map(cleanLine).filter(Boolean);let bank='',account='',amount='';const plate=findPlate(b);const bankIdx=lines.findIndex(l=>!isDepositor(l)&&bankOf(l));if(bankIdx>=0)bank=bankOf(lines[bankIdx]);const accountLine=lines.find(l=>/\d[\d\s-]{7,}\d/.test(l)&&!findPlate(l)&&!isDateLine(l)&&!/^https?:\/\//i.test(l));if(accountLine)account=(accountLine.match(/\d[\d\s-]{7,}\d/)||[''])[0].replace(/[\s-]/g,'');
const amountCandidates=lines.filter(l=>!/^https?:\/\//i.test(l)&&!findPlate(l)&&!(l.match(/\d[\d\s-]{7,}\d/)&&!/(?:원|만원|억)/.test(l))).map(l=>parseKoreanAmount(l)).filter(Boolean);if(amountCandidates.length===1)amount=amountCandidates[0];
return {bank,account,amount,plate,source:b}}

const BANK_CODES={'하나':'081','국민':'004','신한':'088','우리':'020','농협':'011','기업':'003','산업':'002','수협':'007','SC제일':'023','한국씨티':'027',
 'iM뱅크(대구)':'031','부산':'032','경남':'039','광주':'034','전북':'037','제주':'035',
 '카카오뱅크':'090','케이뱅크':'089','토스뱅크':'092',
 '새마을금고':'045','신협':'048','우체국':'071','저축':'050','산림조합':'064',
 '도이치뱅크':'055','중국':'063','중국건설':'067','중국공상':'062','BNP파리바':'061','BOA':'060','HSBC':'054','JP모간':'057',
 '교보':'261','다올투자':'227','대신':'267','메리츠':'287','미래에셋':'238','부국':'290','삼성':'240','상상인':'221','신영':'291','신한투자':'278','아이엠':'262','우리투자':'294','유안타':'209','유진':'280','카카오페이':'288','케이프':'292','키움':'264','토스':'271','하나증권':'270','한국투자':'243','한화투자':'269','현대차':'263','BNK투자':'224','DB':'279','IBK투자':'225','KB':'218','LS':'265','NH투자':'247','SK':'266'};
const BANK_OPTIONS=Object.keys(BANK_CODES), LIMIT=100000000, MAX=100;
const PLATE_RE=/^(?:[가-힣]{2})?\d{2,3}[가-힣]\d{4}$/;
let rows=[], sequence=0;
const uid=()=>String(++sequence);
const won=n=>Number(n||0).toLocaleString('ko-KR');
function parseForReview(text) {
  const result = parseOne(text);
  const rawLines = text.split('\n');
  let nextLine = 0;
  const accounts = result.items.map(item => {
    if (!item.account) return -1;
    const index = rawLines.findIndex((line, i) => i >= nextLine && !/^https?:\/\//i.test(line.trim())
      && line.replace(/\D/g, '') === item.account);
    if (index >= 0) nextLine = index + 1;
    return index;
  });
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
function check(r){
  const errors=[],reviews=[];
  if(!BANK_OPTIONS.includes(r.bank))errors.push('은행 선택');
  if(!/^\d{8,16}$/.test(r.account))errors.push(/\D/.test(r.account)&&/^[\d\s-]+$/.test(r.account)?'계좌번호 하이픈·공백':'계좌번호 확인');
  // 금액 경우의 수: 없음 / 숫자 아님(소수·음수·글자 포함) / 0원 / 너무 큼 / 1억 초과(분할 전)
  const s=String(r.amount??'').trim(), a=Number(s);
  if(s==='')errors.push('금액 없음');
  else if(!/^\d+$/.test(s))errors.push('금액 형식 · 숫자만 입력 (원 단위 소수점 불가)');
  else if(a===0)errors.push('금액 0원');
  else if(!Number.isSafeInteger(a))errors.push('금액 너무 큼');
  else if(a>LIMIT)errors.push(Math.ceil(a/LIMIT)>MAX?`금액 너무 큼 · 나누면 ${MAX}건 초과`:'1억 초과 · 분할 필요');
  if(!r.plate&&!r.inMemo)reviews.push(r.ambiguous?'차량번호 여러 개 · 직접 입력':'차량번호·메모 없음');
  else if(r.plate&&!PLATE_RE.test(r.plate))reviews.push('차량번호 형식');
  if(/^01[016789]\d{7,8}$/.test(r.account))reviews.push('휴대폰 번호 형태 계좌 · 원문 확인');
  if(r.account&&rows.filter(x=>x.bank===r.bank&&x.account===r.account&&!(r.splitGroup&&x.splitGroup===r.splitGroup)).length>1)reviews.push(rows.some(x=>x!==r&&x.bank===r.bank&&x.account===r.account&&x.amount===r.amount&&!(r.splitGroup&&x.splitGroup===r.splitGroup))?'같은 계좌·같은 금액 중복 · 두 번 송금 주의':'같은 계좌 중복');
  if(r.splitGroup)reviews.push('1억 이상 · 분할 송금');else if(a===LIMIT)reviews.push('1억 이상');
  return {errors,reviews,state:errors.length?'bad':reviews.length?'warn':'ok'};
}
function splitRow(r){
  const a=Number(r.amount);if(!(Number.isSafeInteger(a)&&a>LIMIT))return [r];
  const full=Math.floor(a/LIMIT),rest=a-full*LIMIT;
  if(full+(rest?1:0)>MAX)return [r]; // 나누면 100건을 넘는 금액은 나누지 않고 오류로 남겨요 (화면 멈춤 방지)
  const parts=Array(full).fill(LIMIT);if(rest)parts.push(rest);
  const g=r.splitGroup||'g'+uid();
  return parts.map((v,k)=>({...r,id:k?uid():r.id,amount:String(v),splitGroup:g}));
}
function splitAll(list){return list.flatMap(splitRow);}
function amountText(n){
  const v=Number(n||0);
  if(v>0&&v%10000===0) return `${(v/10000).toLocaleString('ko-KR')}만원`;
  return `${won(v)}원`;
}
// 코멘트 한 건의 대상: 딜러(차대금) 또는 은행+계좌
function commentTarget(r){
  const src=r.ctx||r.source||'';
  if(/차량\s*대금|차대금|환급/.test(src)) return {key:'dealer',label:'차대금',to:'딜러에게'};
  // 요청문에 "(압류 계좌)" 같은 구분 문구가 있으면 그 문구로 적어요.
  if(r.label){const c=r.label.charCodeAt(r.label.length-1),j=c>=0xAC00&&c<=0xD7A3?(c-0xAC00)%28:0;return {key:'label',label:'금액',to:`${r.label}${j&&j!==8?'으로':'로'}`};}
  const bank=r.bank||'', acc=r.account||'';
  if(bank&&acc) return {key:bank+'|'+acc,label:'금액',to:`${bank} ${acc} 계좌정보로`};
  return {key:'?'+r.id,label:'금액',to:`[${!bank?'은행':''}${!bank&&!acc?'·':''}${!acc?'계좌번호':''} 확인 필요]`};
}
// 같은 차량번호는 한 줄로: "차대금 1,000만원 딜러에게, 금액 500만원 국민 123 계좌정보로 송금완료"
// 금액은 건별로 적되, 1억 초과로 자동 분할된 줄만 원래 금액으로 합침. 차량번호가 없는 줄은 따로 한 줄씩.
function buildComments(){
  const groups=[],byPlate=new Map();
  rows.forEach(r=>{
    let g=r.plate?byPlate.get(r.plate):null;
    if(!g){g={parts:[],byKey:new Map()};groups.push(g);if(r.plate)byPlate.set(r.plate,g);}
    const t=commentTarget(r),a=Number(r.amount)||0;
    const k=r.splitGroup?'split|'+r.splitGroup:'row|'+r.id;const p=g.byKey.get(k);
    if(p)p.amount+=a;else{const np={...t,amount:a};g.byKey.set(k,np);g.parts.push(np);}
  });
  // 여러 건이 묶인 줄은 맨 끝에 총 금액을 적어요.
  return groups.map(g=>g.parts.map(p=>`${p.label} ${amountText(p.amount)} ${p.to}`).join(', ')+' 송금완료'+(g.parts.length>1?` (총 ${amountText(g.parts.reduce((t,p)=>t+p.amount,0))})`:''));
}
function fileName(stamp){
  // 차량번호별 총 송금금액을 파일명에 표시. 4대 이상이면 "첫번호외N대_총금액"
  const sums=new Map();let none=0,total=0;
  rows.forEach(r=>{const a=Number(r.amount);if(!Number.isSafeInteger(a)||a<=0)return;total+=a;if(r.plate)sums.set(r.plate,(sums.get(r.plate)||0)+a);else none+=a;});
  rows.forEach(r=>{if(r.plate&&!sums.has(r.plate))sums.set(r.plate,0);});
  const plates=[...sums.keys()];
  let tag='';
  if(plates.length>=1&&plates.length<=3){tag=plates.map(p=>`_${p}_${amountText(sums.get(p))}`).join('');if(none)tag+=`_번호없음_${amountText(none)}`;}
  else if(plates.length>3)tag=`_${plates[0]}외${plates.length-1}대_총${amountText(total)}`;
  else if(total)tag=`_총${amountText(total)}`;
  return `하나은행_다계좌이체_${stamp}${tag}.xlsx`.replace(/[\\/:*?"<>|\s]/g,'');
}
