// ── Mac 앱 연결부 ── 웹 transfer.html의 extract() 안쪽(스레드 1개 처리)을 화면 없이 옮겼어요.
function extractThread(raw,threadIndex,threadId,cmsAll){
  const res=parseOne(raw);
  if(!res.items.length)return {added:[],skipped:0,fresh:0,empty:true};
  const rawLines=raw.split('\n');
  const accIdx=res.items.map(x=>{const d=String(x.account||'');return d?rawLines.findIndex(l=>l.replace(/\D/g,'').includes(d)):-1;});
  const cut=accIdx.map(i=>Math.max(0,i-1));
  const labelIdx=accIdx.map((ai,k)=>{if(ai<0)return -1;for(let j=cut[k]-1;j>=0&&j>=cut[k]-3;j--){const t=rawLines[j].trim();if(!t)continue;return /^[(（][^()（）]{1,30}[)）]$/.test(t)?j:-1;}return -1;});
  const startOf=k=>labelIdx[k]>=0?labelIdx[k]:cut[k];
  const before=new Map();const seenG=new Set();
  rows.forEach(r=>{if(r.threadId!==threadId||!r.srcKey)return;const g=r.splitGroup||r.id;if(seenG.has(g))return;seenG.add(g);before.set(r.srcKey,(before.get(r.srcKey)||0)+1);});
  let skipped=0,fresh=0;const added=[];
  res.items.forEach((x,k)=>{
    const srcKey=[x.bank||'',String(x.account||''),String(x.amount||'')].join('|');
    if(before.get(srcKey)){before.set(srcKey,before.get(srcKey)-1);skipped++;return;}
    fresh++;
    const s=k===0?0:startOf(k);
    const e=k+1<res.items.length&&accIdx[k+1]>=0?startOf(k+1):rawLines.length;
    const label=labelIdx[k]>=0?rawLines[labelIdx[k]].trim().slice(1,-1).trim():'';
    const ctx=accIdx[k]>=0?rawLines.slice(s,e).join('\n'):(x.source||'');
    added.push({id:uid(),bank:fullBank(x.bank),account:String(x.account||''),amount:String(x.amount||''),plate:x.plate||'',outMemo:x.plate||'',inMemo:x.plate||'',cms:cmsAll||'',source:x.source||'',ctx,label,holder:holderOf(ctx),thread:threadIndex+1,threadId,srcKey,ambiguous:!!res.ambiguous&&!x.plate});
  });
  return {added,skipped,fresh,empty:false};
}
globalThis.Transfer={
  setRows(v){rows=v;}, newId:()=>uid(), banks:BANK_GROUPS, codes:BANK_CODES, MAX, LIMIT,
  extractThread, splitAll, check:list=>{rows=list;return list.map(check);},
  splitLabel:list=>{rows=list;return list.map(splitLabel);},
  comments:list=>{rows=list;return buildComments();},
  fileRows:list=>{rows=list;return fileRows();},
  xlsx:list=>{rows=list;return xlsxBlob(fileRows());},
  fileName:(list,stamp)=>{rows=list;return fileName(stamp);},
  sentKeys:list=>list.map(sentKey), plateOK:v=>PLATE_RE.test(v), won,
};
