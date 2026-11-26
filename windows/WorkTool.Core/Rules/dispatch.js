/* 차량 조회 규칙: 브라우저와 검증 코드가 함께 사용한다. */
(()=>{
const statuses={confirmed:'금액 확인',missing:'미조회',incomplete:'완료 건 없음',duplicate:'완료 중복',cost:'비용 확인',slack:'슬랙봇 문의',carrier:'캐리어탁송 · 문의 제외'};
const meaning=v=>v==='A'?'주유/부대비용 총액':v==='B'?'주유비':'슬랙봇 문의';
const clean=v=>String(v??'').normalize('NFC').trim();
function plate(value){
 let s=String(value??'').normalize('NFC').replace(/[\s\u200b\ufeff]/g,'');
 const m=s.match(/^([가-힣]{2})?(\d{2,3})(.*)$/);if(!m)return s;
 const rest=m[3],ls='ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ',vs='ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ',ts='ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ';
 let l=ls.indexOf(rest[0]),v=vs.indexOf(rest[1]);if(l<0||v<0)return s;
 let t=ts.indexOf(rest[2]),used=t>=0?3:2;t=t>=0?t+1:0;
 return (m[1]??'')+m[2]+String.fromCharCode(44032+l*588+v*28+t)+rest.slice(used);
}
const valid=p=>/^(?:[가-힣]{2})?\d{2,3}[가-힣]\d{4}$/.test(plate(p));
function parseInput(text){
 const vehicles=[],invalid=[];
 for(const raw of text.split(/\r\n|[\r\n\u2028\u2029]/)){
  let s=raw.trim().normalize('NFC').replace(/[–‑−]/g,'-');if(!s||s.startsWith('전체'))continue;
  let date='';const m=s.match(/^(\d{4})-(\d{2})-(\d{2})\s+(\d{2}):(\d{2})/);
  if(m){const [y,mo,d,h,mi]=m.slice(1).map(Number);const check=new Date(0);check.setUTCFullYear(y,mo-1,d);check.setUTCHours(h,mi,0,0);
   if(check.getUTCFullYear()!==y||check.getUTCMonth()!==mo-1||check.getUTCDate()!==d||check.getUTCHours()!==h||check.getUTCMinutes()!==mi){invalid.push(raw);continue;}
   date=`${m[1]}-${m[2]}-${m[3]} ${m[4]}:${m[5]}`;s=s.slice(m[0].length);
  }
  const p=plate(s);if(!valid(p)){invalid.push(raw);continue;}vehicles.push({id:vehicles.length,plate:p,date,raw});
 }
 return {vehicles,invalid,duplicates:vehicles.length-new Set(vehicles.map(v=>v.plate)).size,cleaned:vehicles.map(v=>v.date?`${v.date}  ${v.plate}`:v.plate).join('\n')};
}
function money(value){const s=String(value??'').normalize('NFKC').replace(/[,\s원₩]/g,'');if(!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(s))return null;const n=Number(s);return Number.isFinite(n)&&Math.abs(n)<=Number.MAX_SAFE_INTEGER?n:null;}
function round10(n){return Math.sign(n)*Math.round((Math.abs(n)+Number.EPSILON)/10)*10;}
const vendorName=v=>({A:'한빛탁송',B:'오픈마일',C:'렌톡'})[v]||v;
const complete=r=>normalizeDispatchState(r.state)==='탁송완료';
const normalizeDispatchState=value=>{const t=String(value??'').replace(/\s+/g,'').trim();if(!t)return '기타/미확인';if(/취소|캔슬|cancelled|canceled|cancel/i.test(t))return '탁송취소';if(/탁송완료|배송완료|운행완료|배달완료|인도완료|^완료$/.test(t))return '탁송완료';if(/탁송중|운행중|운송중|배송중|이동중|진행중|^진행$|출발|픽업중|인수중/.test(t))return '탁송 중';if(/드라이버매칭|배정완료|배차완료|배정|배차|예약|접수|대기/.test(t))return '배정완료';return '기타/미확인';};
const jeju=r=>r.vendor==='C'&&/제주특별자치도|제주도/.test(r.origin||'');
function dateText(v){
 if(v===null||v===undefined||String(v).trim()==='')return '';
 if((typeof v==='number'||/^\d{5}(?:\.\d+)?$/.test(String(v).trim()))&&Number(v)>1000&&Number(v)<100000){const d=new Date(Date.UTC(1899,11,30)+Math.round(Number(v)*86400000));return d.toISOString().slice(0,10);}
 const t=String(v).trim();const m=t.match(/^(\d{4})[.\/-](\d{1,2})[.\/-](\d{1,2})/);
 if(m)return `${m[1]}-${m[2].padStart(2,'0')}-${m[3].padStart(2,'0')}`;
 if(/^\d{8}$/.test(t))return `${t.slice(0,4)}-${t.slice(4,6)}-${t.slice(6,8)}`;
 return t;
}
// Absolute source-column rule: 렌톡 D / 오픈마일 C / 한빛 K + L.
// Values are normalized to local date-time text; Excel serials are not shifted by timezone.
function excelDateTime(value){
 const epoch=Date.UTC(1899,11,30);
 const ms=epoch+Math.round(Number(value)*1440)*60000;
 if(!Number.isFinite(ms))return '';
 const d=new Date(ms);
 if(!Number.isFinite(d.getTime()))return '';
 const day=d.toISOString().slice(0,10),time=d.toISOString().slice(11,16);
 return time==='00:00'?day:day+' '+time;
}
function scheduleTime(value){
 if(value==null||String(value).trim()==='')return '';
 if(value instanceof Date)return Number.isFinite(value.getTime())?value.toISOString().slice(11,16):'';
 if((typeof value==='number'||/^(?:0(?:\.\d+)?|1(?:\.0+)?)$/.test(String(value).trim()))&&Number(value)>=0&&Number(value)<1){const mins=Math.round(Number(value)*1440)%1440;return String(Math.floor(mins/60)).padStart(2,'0')+':'+String(mins%60).padStart(2,'0');}
 const text=String(value).trim();
 const match=text.match(/^(오전|오후|AM|PM)?\s*(\d{1,2})(?::(\d{2})(?::\d{2}(?:\.\d+)?)?|\s*시(?:\s*(\d{1,2})\s*분)?)\s*(AM|PM)?$/i);
 if(match){let hour=Number(match[2]),min=Number(match[3]??match[4]??0);if(hour>23||min>59)return text;
  const meridiem=(match[1]||match[5]||'').toUpperCase();
  if(meridiem==='오전'||meridiem==='AM')hour=hour===12?0:hour;
  if(meridiem==='오후'||meridiem==='PM')hour=hour===12?12:hour+12;
  return String(hour).padStart(2,'0')+':'+String(min).padStart(2,'0');
 }
 return text;
}
function scheduleDateTime(value){
 if(value==null||String(value).trim()==='')return '';
 if(value instanceof Date)return Number.isFinite(value.getTime())?excelDateTime((value.getTime()-Date.UTC(1899,11,30))/86400000):'';
 if((typeof value==='number'||/^\d{4,5}(?:\.\d+)?$/.test(String(value).trim()))&&Number(value)>=1000&&Number(value)<100000)return excelDateTime(value);
 // 보이지 않는 문자(폭 없는 공백·BOM)는 지우고 읽어요.
 const t=String(value).normalize('NFKC').replace(/[\u200B-\u200D\u2060\uFEFF]/g,'').trim();
 const match=t.match(/^(\d{4})\s*[.\/\-]\s*(\d{1,2})\s*[.\/\-]\s*(\d{1,2})\.?(?:\s*\([월화수목금토일]\))?(?:\s*T?\s*(.*))?$/);
 if(match){const day=match[1]+'-'+match[2].padStart(2,'0')+'-'+match[3].padStart(2,'0');const time=scheduleTime(match[4]);if(!match[4]||/^\d{2}:\d{2}$/.test(time))return time?day+' '+time:day;}
 const digits=t.match(/^(\d{4})(\d{2})(\d{2})(?:\s+(.*))?$/);
 if(digits){const day=digits[1]+'-'+digits[2]+'-'+digits[3];const time=scheduleTime(digits[4]);if(!digits[4]||/^\d{2}:\d{2}$/.test(time))return time?day+' '+time:day;}
 // 따옴표·괄호·시간대(+09:00)·뒤에 붙은 글자가 있어도 안에 든 날짜·시간을 찾아 맞춰요.
 const inside=t.match(/(\d{4})\s*[.\/\-]\s*(\d{1,2})\s*[.\/\-]\s*(\d{1,2})\.?(?:\s*\([월화수목금토일]\))?(?:\s*T?\s*((?:오전|오후|AM|PM)?\s*\d{1,2}:\d{2}(?::\d{2}(?:\.\d+)?)?(?:\s*(?:AM|PM))?))?/i);
 if(inside&&Number(inside[2])>=1&&Number(inside[2])<=12&&Number(inside[3])>=1&&Number(inside[3])<=31){
  const day=inside[1]+'-'+inside[2].padStart(2,'0')+'-'+inside[3].padStart(2,'0');const time=inside[4]?scheduleTime(inside[4]):'';
  if(!inside[4]||/^\d{2}:\d{2}$/.test(time))return time?day+' '+time:day;
 }
 return t;
}
// 탁송일정은 3사 모두 'YYYY-MM-DD HH:MM'으로 맞춰요. 시간이 없는 건은 00:00을 붙여요.
function dispatchSchedule(vendor,row){
 let value;
 if(vendor==='A'){
  const date=scheduleDateTime(row[10]);const time=scheduleTime(row[11]);
  value=time?(date?date.slice(0,10)+' '+time:time):date;
 }else value=scheduleDateTime(vendor==='B'?row[2]:row[3]);
 return /^\d{4}-\d{2}-\d{2}$/.test(value)?value+' 00:00':value;
}
// 업체 양식 점검: 열 위치로 읽는 값(탁송일정·탁송비용·캐리어 구분)이 절반 넘게 예상과 다르면
// 업체가 열을 끼워 넣거나 옮겼을 수 있어서 경고해요. 값은 그대로 읽고, 경고만 붙여요.
const FIXED_COLUMNS={A:{schedule:'K·L',fee:'AP'},B:{schedule:'C',fee:'U'},C:{schedule:'D',fee:'S',carrier:'J'}};
const FIXED_LABELS={schedule:'탁송일정',fee:'탁송비용',carrier:'캐리어 구분'};
function layoutWarnings(sheetName,vendor,list){
 const stat={schedule:[0,0],fee:[0,0],carrier:[0,0]};
 for(const r of list){
  const s=String(r.dispatchSchedule??'').trim();
  if(s){stat.schedule[0]++;if(!/^\d{4}-\d{2}-\d{2}(?: \d{2}:\d{2})?$|^\d{2}:\d{2}$/.test(s))stat.schedule[1]++;}
  if(String(r.dispatchFeeRaw??'').trim()!==''){stat.fee[0]++;if(r.dispatchFee===null||r.dispatchFee<0)stat.fee[1]++;}
  if(vendor==='C'&&r.carrierFlag){stat.carrier[0]++;if(r.carrierFlag!=='Y'&&r.carrierFlag!=='N')stat.carrier[1]++;}
 }
 const cols=FIXED_COLUMNS[vendor]||{};
 return Object.keys(stat).flatMap(k=>{const [n,bad]=stat[k];
  return cols[k]&&bad>0&&bad*2>n?[`${sheetName} 시트: ${FIXED_LABELS[k]}(${cols[k]}열) ${n}건 중 ${bad}건이 예상한 형식이 아니에요. 업체 양식이 바뀌었는지 확인해 주세요.`]:[];});
}
function readSheets(sheets,name){
 const records=[],recognized=[],warnings=[];let ignored=0;
 for(const sheet of sheets){let header=null,vendor,hi;const first=records.length;
  for(let i=0;i<Math.min(30,sheet.rows.length);i++){
   const h=new Map();sheet.rows[i].forEach((v,j)=>{if(!h.has(clean(v)))h.set(clean(v),j);});
   if(h.has('출발지 차량번호')&&h.has('운행상태')&&h.has('출발지 주소(현재차량 상세위치)'))vendor='C';
   else if(h.has('차량번호')&&h.has('상태')&&h.has('주유/부대비용 총액'))vendor='A';
   else if(h.has('차량번호')&&h.has('상태')&&h.has('주유비'))vendor='B';else continue;
   header=h;hi=i;break;
  }
  if(!header)continue;if(sheet.rows.length-hi>100001)throw Error('한 시트는 100,000행까지 지원해요.');recognized.push(sheet.name);
  for(let i=hi+1;i<sheet.rows.length;i++){
   const row=sheet.rows[i],get=k=>clean(row[header.get(k)]);const p=plate(get(vendor==='C'?'출발지 차량번호':'차량번호'));
   if(!valid(p)){if(row.some(v=>clean(v)))ignored++;continue;}
   const rawCost=vendor==='C'?'':get(vendor==='A'?'주유/부대비용 총액':'주유비');
   records.push({plate:p,vendor,state:get(vendor==='C'?'운행상태':'상태'),cost:vendor==='C'?null:(vendor==='A'&&String(rawCost??'').trim()===''?0:money(rawCost)),rawCost,dispatchSchedule:dispatchSchedule(vendor,row),dispatchFeeRaw:row[vendor==='A'?41:vendor==='B'?20:18]??'',dispatchFee:money(row[vendor==='A'?41:vendor==='B'?20:18]),carrierFlag:vendor==='C'?String(row[9]??'').trim().toUpperCase():'',origin:get(vendor==='C'?'출발지 주소(현재차량 상세위치)':vendor==='A'?'출발지 주소':'출발지'),dispatchDate:dateText(vendor==='A'?row[10]:vendor==='B'?row[2]:row[3]),file:name,sheet:sheet.name,row:(sheet.startRow??1)+i});
  }
  warnings.push(...layoutWarnings(sheet.name,vendor,records.slice(first)));
 }
 if(!recognized.length)throw Error('지원하는 A·B·C 양식을 찾지 못했어요. 원래 제목 행이 있는 .xlsx 파일을 첨부해 주세요.');
 return {name,records,sheets:recognized,ignored,warnings};
}
// 업체 A·B·C 구분 없이 동일 차량번호의 전체 기록에서 탁송일자가 가장 최신인 한 건만 적용해요.
// 날짜가 같으면 나중에 불러온 파일/아래쪽 행을 우선하고, 날짜가 없는 건은 후순위예요.
function candidateList(ranked,field){
 return ranked.flatMap(({r,i})=>{
  const value=r[field];
  if(field==='dispatchFee'?value===null||value<0||!Number.isFinite(value):!String(value??'').trim())return [];
  return [{id:String(i),value,vendor:r.vendor,source:(r.file||'파일')+' / '+(r.sheet||'시트')+' / '+(r.row??'?')+'행',state:normalizeDispatchState(r.state)}];
 });
}
function useCandidate(result,field,id){
 const list=field==='schedule'?result.scheduleCandidates:field==='fee'?result.feeCandidates:null;
 if(!list)return false;
 const chosen=list.find(item=>item.id===String(id));
 if(!chosen)return false;
 if(field==='schedule'){
  result.selectedScheduleId=chosen.id;
  result.schedule=chosen.value;
  result.scheduleConflict=false;
  const matchingFee=result.feeCandidates.find(item=>item.id===chosen.id);
  result.selectedFeeId=matchingFee?.id??'';
  result.dispatchFee=matchingFee?.value??null;
  result.vat=matchingFee?Math.round((matchingFee.value*10)/100):null;
  result.feeConflict=!matchingFee;

 }else{
  result.selectedFeeId=chosen.id;
  result.dispatchFee=chosen.value;
  result.vat=Math.round((chosen.value*10)/100);
  result.feeConflict=false;
 }
 return true;
}
function hasUnresolvedChoice(row){return !!(row.scheduleConflict||row.feeConflict);}
function match(input,files){
 const source=new Map(),counts=new Map();
 // 업체별로 분리하지 않고 차량번호 하나로 모든 업체 기록을 합쳐요.
 for(const f of files)for(const r of f.records){if(!source.has(r.plate))source.set(r.plate,[]);source.get(r.plate).push(r);}
 input.forEach(v=>counts.set(v.plate,(counts.get(v.plate)??0)+1));
 return input.map(v=>{
  const all=source.get(v.plate)??[];
  const ranked=all.map((r,i)=>({r,i,date:/^\d{4}-\d{2}-\d{2}$/.test(r.dispatchDate||'')?r.dispatchDate:''}));
  ranked.sort((a,b)=>b.date.localeCompare(a.date)||b.i-a.i);
  const selected=ranked[0]?.r;
  const rentok=ranked.find(x=>x.r.vendor==='C')?.r;
  const carrierFlag=rentok?.carrierFlag??'';
  const carrierType=!rentok?'—':carrierFlag==='Y'?'캐리어탁송':carrierFlag==='N'?'일반탁송':'확인 필요';
  const scheduleCandidates=candidateList(ranked,'dispatchSchedule');
  const feeCandidates=candidateList(ranked,'dispatchFee');
  const scheduleConflict=new Set(scheduleCandidates.map(item=>item.value)).size>1;
  const feeConflict=new Set(feeCandidates.map(item=>item.value)).size>1;
  const chosenSchedule=scheduleConflict?null:scheduleCandidates[0]??null;
  const chosenFee=feeConflict?null:feeCandidates[0]??null;
  const done=selected&&complete(selected)?[selected]:[];
  let status,amount=null,costMeaning='';
  if(!selected)status='missing';
  else if(!done.length)status='incomplete';
  else if(selected.vendor==='C'){status=carrierFlag==='Y'?'carrier':'slack';costMeaning=carrierFlag==='Y'?'캐리어탁송 (문의 제외)':meaning('C');}
  else if(selected.cost!==null){status='confirmed';amount=round10(selected.cost);costMeaning=meaning(selected.vendor);}
  else {status='cost';costMeaning=meaning(selected.vendor);}
  const rawStates=selected?[normalizeDispatchState(selected.state)]:[];
  return {...v,status,rawStates,rawState:rawStates.join(', '),amount,meaning:costMeaning,
   vendor:selected?vendorName(selected.vendor):'',dispatchDate:selected?.dispatchDate??'',
   schedule:chosenSchedule?.value??'',selectedScheduleId:chosenSchedule?.id??'',scheduleCandidates,scheduleConflict,
   dispatchFee:chosenFee?.value??null,vat:chosenFee?Math.round((chosenFee.value*10)/100):null,selectedFeeId:chosenFee?.id??'',feeCandidates,feeConflict,
   dispatchDates:selected?.dispatchDate?[selected.dispatchDate]:[],origin:selected?.origin??'',
   inputDuplicate:counts.get(v.plate)>1,jeju:!!selected&&jeju(selected),
   carrier:carrierFlag==='Y',carrierFlag,carrierType,
   cInquiry:!!selected&&complete(selected)&&selected.vendor==='C'&&carrierFlag!=='Y',
   sources:selected?`${selected.file} / ${selected.sheet} / ${selected.row}행`:'',
   matchedCount:all.length};
 });
}
function unique(rows){return [...new Map(rows.map(r=>[r.plate,r])).values()];}
// 진행 중: 기록은 있지만 아직 탁송완료가 아니고, 탁송 중이거나 배정완료인 차량 (취소·기타 상태는 제외)
const inProgress=r=>r.status==='incomplete'&&(r.rawState==='탁송 중'||r.rawState==='배정완료');
const slackPending='확인 중';
const statusLabel=r=>r.slackPending?slackPending:inProgress(r)?'진행 중':statuses[r.status];
function includes(r,f){return f==='all'||(f==='confirmed'&&r.status==='confirmed')||(f==='missing'&&r.status==='missing')||(f==='progress'&&inProgress(r))||(f==='jeju'&&r.jeju)||(f==='carrier'&&r.carrier)||(f==='slack'&&r.cInquiry&&(!r.slackReviewed||r.slackPending))||(f==='slack-replies'&&r.slackReviewed)||(f==='review'&&((r.status!=='confirmed'&&r.status!=='carrier')||r.inputDuplicate||r.carrierType==='확인 필요'||hasUnresolvedChoice(r)));}
function slackValue(value){
 const token=value.trim();
 if(token.replace(/\s+/g,'')==='확인중')return {amount:null};
 if(token.toUpperCase()==='X')return {amount:0};
 const match=token.match(/^(?:₩\s*)?(?<number>[0-9]+|[0-9]{1,3}(?<separator>[,.])[0-9]{3}(?:\k<separator>[0-9]{3})*)\s*원?$/);
 if(!match)return null;
 const amount=Number(match.groups.number.replace(/[,.]/g,''));
 return Number.isSafeInteger(amount)?{amount:round10(amount)}:null;
}
function applySlackReplies(text,rows){
 const eligible=new Map(), replies=new Map(), conflicts=new Set(), issues=[];
 for(const row of rows){
  if(!row.cInquiry)continue;
  if(!eligible.has(row.plate))eligible.set(row.plate,[]);
  eligible.get(row.plate).push(row);
 }
 text.split(/\r\n|\r|\n/).forEach((raw,index)=>{
  const line=raw.trim().normalize('NFKC');
  if(!line)return;
  const split=line.match(/^(.+?)\s*[-–—−]\s*(.+)$/);
  const key=split?plate(split[1]):'';
  const value=split?slackValue(split[2]):null;
  if(!valid(key)||!value){issues.push(`${index+1}줄: 차량번호와 값 형식을 확인해 주세요.`);return;}
  if(!eligible.has(key)){issues.push(`${index+1}줄: ${key}은(는) 현재 슬랙봇 확인 대상이 아니에요.`);return;}
  if(replies.has(key)&&replies.get(key)!==value.amount){
   if(!conflicts.has(key))issues.push(`${key}: 서로 다른 값이 있어 반영하지 않았어요.`);
   conflicts.add(key);
  }else replies.set(key,value.amount);
 });
 let applied=0;
 for(const [key,amount] of replies){
  if(conflicts.has(key))continue;
  for(const row of eligible.get(key)){
   row.slackPending=amount===null;
   row.slackReviewed=true;
   row.amount=amount;
   row.status=amount===null?'slack':'confirmed';
   row.meaning='슬랙봇 확인';
  }
  applied++;
 }
 return {applied,issues};
}
const notes=r=>[r.inputDuplicate?'입력 중복':'',r.jeju?'제주 출발':'',r.carrier?'캐리어탁송':r.carrierType==='확인 필요'?'캐리어 여부 확인 필요':'',r.cInquiry?'렌톡 문의':'',r.scheduleConflict?'일정 중첩 · 선택 필요':'',r.feeConflict?'탁송비용 중첩 · 선택 필요':''].filter(Boolean).join(' · ');
const headers=['탁송일정','차량번호','주유비용/충전비용(원)','탁송비용(원)','부가세(원)','비용 구분','조회 상태','입력 일시','업체','탁송 구분','출발지','메모','원본 위치'];
const values=r=>[r.schedule,r.plate,r.slackPending?slackPending:r.amount,r.dispatchFee,r.vat,r.meaning,statusLabel(r),r.date,r.vendor,r.carrierType,r.origin,notes(r),r.sources];
function textExport(rows){return '\ufeff'+[headers,...unique(rows).map(values)].map(row=>row.map(v=>String(v??'').replace(/[\t\r\n]/g,' ')).join('\t')).join('\r\n');}
globalThis.DispatchCore={statuses,inProgress,statusLabel,meaning,plate,valid,parseInput,money,round10,complete,jeju,readSheets,match,unique,includes,notes,headers,values,textExport,dispatchSchedule,scheduleDateTime,scheduleTime,useCandidate,hasUnresolvedChoice,slackPending,applySlackReplies};
})();
