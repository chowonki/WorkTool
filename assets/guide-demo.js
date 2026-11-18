/* 그림 사용법의 체험 화면 — 가상 데이터로 실제 도구처럼 눌러 볼 수 있어요. 아무것도 저장하거나 내려받지 않아요. */
(function(){
'use strict';
const won=n=>Number(n).toLocaleString('ko-KR');
function kit(root){
  const el=n=>root.querySelector('[data-el="'+n+'"]');
  let tid=0;
  const toast=msg=>{const t=el('toast');t.textContent=msg;t.classList.add('show');clearTimeout(tid);tid=setTimeout(()=>t.classList.remove('show'),2600);};
  const copy=(text,label)=>{try{navigator.clipboard&&navigator.clipboard.writeText(text).catch(()=>{});}catch(e){}toast((label||text)+' 복사했어요');};
  const coach=(text,target)=>{el('coach').innerHTML=text;root.querySelectorAll('.is-next').forEach(x=>x.classList.remove('is-next'));if(target)target.classList.add('is-next');};
  return {el,toast,copy,coach};
}

/* ───────── 탁송체크 ───────── */
function dispatchDemo(root){
  const {el,toast,copy,coach}=kit(root);
  const LIST='2026-11-03 09:30\t12서7567\n2026-11-03\t55노4983\n2026-11-04\t02두3821\n2026-11-06\t39수7009\n2026-11-05\t25마7579';
  const FILES=[['한빛탁송.xlsx',3],['오픈마일.xlsx',4],['렌톡.xlsx',3]];
  const BASE=[
    {date:'11-03 09:30',car:'12서7567',fuel:12350,fee:28000,st:'ok'},
    {date:'11-03',car:'55노4983',fuel:0,fee:26000,st:'ok'},
    {date:'11-04',car:'02두3821',fuel:8700,fee:30000,st:'ok',jeju:true},
    {date:'11-06',car:'39수7009',fuel:null,fee:28000,st:'progress'},
    {date:null,car:'25마7579',fuel:20000,fee:null,st:'pick',carrier:true,
     picks:[{date:'11-05 08:10',vendor:'한빛탁송',fee:24000},{date:'11-05 14:40',vendor:'오픈마일',fee:22000}]}
  ];
  const TABS=[['all','전체'],['ok','금액 확인'],['progress','진행 중'],['jeju','제주 출발'],['carrier','캐리어탁송'],['check','확인 필요']];
  let S;
  const reset=()=>{S={files:0,done:false,filter:'all',rows:BASE.map(r=>Object.assign({},r))};el('list').value='';el('files').innerHTML='';render();};
  const cars=()=>(el('list').value.match(/\d{2,3}[가-힣]\d{4}/g)||[]);
  const match=(r,f)=>f==='all'||(f==='ok'&&r.st==='ok')||(f==='progress'&&r.st==='progress')||(f==='jeju'&&r.jeju)||(f==='carrier'&&r.carrier)||(f==='check'&&r.st==='pick');
  const visible=()=>{const want=new Set(cars());return S.rows.filter(r=>want.has(r.car));};
  const badge=r=>r.st==='ok'?'<span class="m-badge ok">금액 확인</span>':r.st==='progress'?'<span class="m-badge wait">진행 중</span>':'<span class="m-badge warn">확인 필요</span>';
  const cell=(v,txt,bold)=>v==null?'—':'<button type="button" class="m-copy'+(bold?' b':'')+'" data-copy="'+v+'">'+txt+'</button>';
  function render(){
    const n=cars().length;
    el('listMeta').textContent=n?n+'대 인식했어요':'0대';
    el('lookup').disabled=!(n&&S.files===3);
    el('result').hidden=!S.done;el('empty').hidden=S.done;
    if(!n)coach('<b>①</b> 차량목록 칸에 차량번호를 붙여넣어 주세요. <b>가상 목록 붙여넣기</b>를 눌러도 돼요.',el('listCard'));
    else if(S.files<3)coach('<b>②</b> <b>업체 파일 첨부</b> 상자를 눌러 3사 시트를 넣어 주세요. ('+S.files+'/3)',el('fileCard'));
    else if(!S.done)coach('<b>③</b> <b>차량 조회하기</b>를 눌러 주세요.',el('lookup'));
    else if(S.rows.some(r=>r.st==='pick'))coach('<b>④·⑤</b> 결과가 나왔어요. <b>확인 필요</b> 카드나 탭을 눌러, 기록이 둘인 차량의 일정을 골라 주세요.',el('tabs'));
    else coach('<b>⑥</b> 값을 눌러 복사하거나 <b>캡처 복사</b>로 결과를 이미지로 보내 보세요. 다 됐으면 <b>처음부터</b>로 다시 해 볼 수 있어요.',el('export'));
    if(!S.done)return;
    const rows=visible();
    const cnt=f=>rows.filter(r=>match(r,f)).length;
    el('stats').innerHTML='<b class="n" aria-hidden="true">4</b>'+[['all','전체 차량'],['ok','금액 확인'],['progress','진행 중'],['jeju','제주 출발'],['check','확인 필요']].map(([f,l],i)=>'<button type="button" class="'+(S.filter===f?'on':'')+(i>2?' m-hide-s':'')+'" data-filter="'+f+'"><small>'+l+'</small><b>'+cnt(f)+'</b></button>').join('');
    el('tabs').innerHTML='<b class="n" aria-hidden="true">5</b>'+TABS.map(([f,l])=>'<button type="button" role="tab" aria-selected="'+(S.filter===f)+'" class="'+(S.filter===f?'on':'')+'" data-filter="'+f+'">'+l+' <em>'+cnt(f)+'</em></button>').join('');
    const shown=rows.filter(r=>match(r,S.filter));
    el('count').textContent=shown.length;
    el('rows').innerHTML=shown.length?shown.map(r=>{
      const date=r.st==='pick'?'<select class="m-pick" data-pick="'+r.car+'" aria-label="'+r.car+' 탁송 기록 고르기"><option value="">선택 필요 ▾</option>'+r.picks.map((p,i)=>'<option value="'+i+'">'+p.date+' · '+p.vendor+'</option>').join('')+'</select>':cell(r.date,r.date);
      const fee=r.st==='pick'?'<span class="m-pickmini">선택 필요</span>':cell(r.fee,won(r.fee));
      return '<tr><td>'+date+'</td><td>'+cell(r.car,r.car,true)+'</td><td class="r">'+cell(r.fuel,r.fuel==null?'':won(r.fuel),true)+'</td><td class="r m-hide-s">'+fee+'</td><td>'+badge(r)+(r.jeju?' <span class="m-tag">제주</span>':'')+(r.carrier?' <span class="m-tag">캐리어</span>':'')+'</td></tr>';
    }).join(''):'<tr><td colspan="5" class="m-none">이 조건에 맞는 차량이 없어요.</td></tr>';
  }
  root.addEventListener('input',e=>{if(e.target===el('list'))render();});
  root.addEventListener('change',e=>{
    const p=e.target.closest('[data-pick]');if(!p||p.value==='')return;
    const r=S.rows.find(x=>x.car===p.dataset.pick),pick=r.picks[+p.value];
    Object.assign(r,{date:pick.date,fee:pick.fee,st:'ok'});toast(r.car+' → '+pick.vendor+' 기록으로 정했어요');render();
  });
  root.addEventListener('click',e=>{
    const b=e.target.closest('button');if(!b)return;
    const act=b.dataset.act;
    if(b.dataset.copy!=null){const v=b.dataset.copy;copy(/^\d+$/.test(v)?v:v,/^\d+$/.test(v)?won(v)+' (쉼표 없이)':v);return;}
    if(b.dataset.filter){S.filter=b.dataset.filter;render();return;}
    if(act==='restart'){reset();toast('처음부터 다시 시작해요');}
    else if(act==='paste'){el('list').value=LIST;render();el('list').focus();}
    else if(act==='files'){
      if(S.files>=3){toast('3사 파일을 이미 넣었어요');return;}
      const [name,c]=FILES[S.files++];
      el('files').insertAdjacentHTML('beforeend','<div class="m-file"><span>X</span>'+name+'<em>'+c+'건</em></div>');
      if(S.done)S.done=false;render();
    }
    else if(act==='lookup'){
      if(b.disabled)return;b.disabled=true;b.classList.add('busy');
      setTimeout(()=>{b.classList.remove('busy');S.done=true;S.filter='all';render();toast(visible().length+'대 조회를 마쳤어요');el('result').scrollIntoView({behavior:'smooth',block:'nearest'});},650);
    }
    else if(act==='copyCars'){const rows=visible().filter(r=>match(r,S.filter));copy(rows.map(r=>r.car).join('\n'),'차량번호 '+rows.length+'대');}
    else if(act==='cap-copy'||act==='cap-png'||act==='file'){
      const scope=el('scope').value==='ok'?visible().filter(r=>r.st==='ok'):visible().filter(r=>match(r,S.filter));
      const what=act==='cap-copy'?'캡처 이미지를 복사':act==='cap-png'?'캡처 PNG를 저장':b.dataset.kind+' 파일을 저장';
      toast(scope.length+'대로 '+what+'하는 버튼이에요. 체험 화면이라 실제로 만들지는 않아요.');
    }
  });
  reset();
}

/* ───────── 하나은행 이체파일 ───────── */
function transferDemo(root){
  const {el,toast,copy,coach}=kit(root);
  const THREAD='[이체 요청] 오늘 탁송비 정산 부탁드려요.\n\n현대 쏘나타 123가4567\n국민은행 123-456-789012\n35,000원\n\n기아 K5 45나6789\n신한은행 110-123-456789\n35,000원\n\n우리은행 1002-345-678901\n금액은 확인 후 알려드릴게요';
  const BANKS=[['004','국민'],['088','신한'],['020','우리'],['081','하나']];
  const BASE=[
    {bank:'004',acct:'123456789012',amt:'35000',car:'123가4567'},
    {bank:'088',acct:'110123456789',amt:'35000',car:'45나6789'},
    {bank:'020',acct:'1002345678901',amt:'',car:''}
  ];
  const GUIDE={
    1:['1단계 · 이체 요청 입력',['Slack 스레드를 그대로 붙여넣어요.','스레드가 여러 개면 입력칸을 추가해요.','<b>이체정보 추출하기</b>를 누르면 2단계로 넘어가요.']],
    2:['2단계 · 추출 결과 확인·수정',['정상·검토·오류 건수를 보고, 누르면 그 줄만 모아 봐요.','은행·계좌·금액을 원문과 대조하고 칸을 눌러 고쳐요.','통장 메모와 CMS코드를 한 번에 넣어요.','빨간 오류를 다 고친 뒤 다음 단계로 가요.']],
    3:['3단계 · 이체파일 다운로드',['미리보기 표가 엑셀에 그대로 들어가요.','검토 필요 건이 있으면 원문과 대조한 뒤 체크해요.','<b>하나은행 이체파일 다운로드</b>로 .xlsx를 받아요.','은행에서 송금을 마친 뒤 Slack에 남길 송금 코멘트를 복사해요.']]
  };
  let S;
  const bankName=c=>(BANKS.find(b=>b[0]===c)||['',''])[1];
  const amount=r=>{const t=String(r.amt).replace(/[,\s원]/g,'');return /^\d+$/.test(t)&&+t>0?+t:null;};
  const status=r=>!/^\d{10,14}$/.test(String(r.acct).replace(/-/g,''))||amount(r)==null?'error':!r.car?'review':'ok';
  const why=r=>!/^\d{10,14}$/.test(String(r.acct).replace(/-/g,''))?'계좌번호 자릿수를 확인해 주세요':amount(r)==null?'입금액이 없어요. 금액을 넣어 주세요':!r.car?'차량번호가 없어요. 원문과 대조해 주세요':'';
  const shortAmt=n=>n%10000===0?(n/10000)+'만원':won(n)+'원';
  const reset=()=>{S={step:1,rows:[],filter:'all',memo:'',ack:false,downloaded:false,threads:1};el('threads').querySelectorAll('.m-extra').forEach(x=>x.remove());el('thread').value='';el('memo').value='';render();};
  const go=n=>{S.step=n;render();root.querySelector('.mock').scrollIntoView({behavior:'smooth',block:'nearest'});};
  function render(){
    root.querySelectorAll('[data-step]').forEach(sec=>sec.hidden=+sec.dataset.step!==S.step);
    root.querySelectorAll('[data-go]').forEach(b=>{if(!b.closest('.m-steps'))return;const n=+b.dataset.go;b.className=n===S.step?'on':n<S.step?'done':'';b.disabled=n>S.step&&!(n===2&&S.rows.length);b.querySelector('i').textContent=n<S.step?'✓':n;});
    const [title,items]=GUIDE[S.step];
    el('guide').innerHTML='<strong>'+title+'</strong><ol>'+items.map(x=>'<li>'+x+'</li>').join('')+'</ol>';
    const err=S.rows.filter(r=>status(r)==='error').length,rev=S.rows.filter(r=>status(r)==='review').length;
    if(S.step===1){
      el('err1').hidden=true;
      if(!el('thread').value.trim())coach('<b>①</b> 입력칸에 Slack 스레드를 붙여넣어 주세요. <b>가상 요청 붙여넣기</b>를 눌러도 돼요.',el('thread').parentNode);
      else coach('<b>③</b> <b>이체정보 추출하기</b>를 눌러 주세요. 스레드가 더 있으면 <b>② 입력칸 추가</b>를 눌러요.',el('extract'));
    }
    if(S.step===2){
      const cnt={all:S.rows.length,ok:S.rows.length-err-rev,review:rev,error:err};
      el('tiles').innerHTML='<b class="n" aria-hidden="true">1</b>'+[['all','전체 건수',''],['ok','정상','g'],['review','검토 필요','y'],['error','오류','red']].map(([f,l,c])=>'<button type="button" data-tile="'+f+'" class="'+(S.filter===f?'on':'')+'"><small>'+l+'</small><b class="'+c+'">'+cnt[f]+'건</b></button>').join('');
      const list=S.rows.map((r,i)=>[r,i]).filter(([r])=>S.filter==='all'||status(r)===S.filter);
      if(!el('trows').contains(document.activeElement)){
        el('trows').innerHTML=list.length?list.map(([r,i])=>{const st=status(r);return '<tr class="st-'+st+'"><td><span class="m-badge '+(st==='ok'?'ok':st==='review'?'wait':'bad')+'" title="'+why(r)+'">'+(st==='ok'?'✓ 정상':st==='review'?'⚠ 검토':'✕ 오류')+'</span></td><td><select class="m-sel" data-i="'+i+'" data-k="bank" aria-label="은행">'+BANKS.map(b=>'<option value="'+b[0]+'"'+(b[0]===r.bank?' selected':'')+'>'+b[1]+' ('+b[0]+')</option>').join('')+'</select></td><td><input class="m-field" data-i="'+i+'" data-k="acct" value="'+r.acct+'" inputmode="numeric" aria-label="계좌번호"></td><td class="r"><input class="m-field r'+(amount(r)==null?' bad':'')+'" data-i="'+i+'" data-k="amt" value="'+(amount(r)?won(amount(r)):r.amt)+'" placeholder="금액" inputmode="numeric" aria-label="입금액"></td></tr>'+(st!=='ok'?'<tr class="m-why"><td colspan="4">'+why(r)+'</td></tr>':'');}).join(''):'<tr><td colspan="4" class="m-none">이 조건에 맞는 줄이 없어요.</td></tr>';
      }else{
        S.rows.forEach((r,i)=>{const tr=el('trows').querySelector('[data-i="'+i+'"]');if(!tr)return;const row=tr.closest('tr'),st=status(r),bd=row.querySelector('.m-badge');bd.className='m-badge '+(st==='ok'?'ok':st==='review'?'wait':'bad');bd.textContent=st==='ok'?'✓ 정상':st==='review'?'⚠ 검토':'✕ 오류';const w=row.nextElementSibling;if(w&&w.classList.contains('m-why'))w.firstChild.textContent=why(r)||'고쳤어요';row.querySelector('[data-k="amt"]').classList.toggle('bad',amount(r)==null);});
        el('tiles').querySelectorAll('b:not(.n)').forEach((b,j)=>{b.textContent=[S.rows.length,S.rows.length-err-rev,rev,err][j]+'건';});
      }
      el('err2').hidden=true;
      if(err)coach('<b>②</b> 빨간 <b>오류</b> 줄의 입금액 칸을 눌러 금액(예: 80,000)을 넣어 주세요. 원문에 금액이 없으면 요청자에게 먼저 확인해요.',el('trows').closest('.m-tablebox'));
      else if(!S.memo)coach('<b>③</b> 오류를 다 고쳤어요. 필요하면 통장 메모를 넣고, <b>④ 다음</b>을 눌러 주세요.',el('next'));
      else coach('<b>④</b> <b>다음: 이체파일 만들기</b>를 눌러 주세요.',el('next'));
    }
    if(S.step===3){
      el('prows').innerHTML=S.rows.map(r=>'<tr><td>'+bankName(r.bank)+'</td><td>'+r.acct.replace(/-/g,'')+'</td><td class="r">'+won(amount(r))+'</td><td>'+(S.memo||r.car||'—')+'</td></tr>').join('');
      el('ackBox').hidden=!rev;el('ack').checked=S.ack;
      el('ackText').textContent='검토 필요 '+rev+'건을 원문과 대조해 확인했어요.';
      el('dl').disabled=!!rev&&!S.ack;
      el('comments').innerHTML=S.rows.map(r=>{const t='금액 '+shortAmt(amount(r))+' '+bankName(r.bank)+' '+r.acct.replace(/-/g,'')+' 계좌정보로 송금완료';return '<div class="m-cmt"><span>'+t+'</span><button type="button" class="m-btn sm" data-comment="'+t+'">복사</button></div>';}).join('');
      if(rev&&!S.ack)coach('<b>②</b> 검토 필요 건을 원문과 대조한 뒤 체크해 주세요. 체크해야 다운로드할 수 있어요.',el('ackBox'));
      else if(!S.downloaded)coach('<b>③</b> <b>하나은행 이체파일 다운로드</b>를 눌러 주세요.',el('dl'));
      else coach('<b>④</b> 은행에서 송금을 마쳤다면 <b>송금 코멘트</b>를 복사해 Slack에 남겨요. 다 됐어요!',el('comments').parentNode);
    }
  }
  root.addEventListener('input',e=>{
    const t=e.target;
    if(t===el('thread')&&S.step===1)render();
    if(t.dataset.k){S.rows[+t.dataset.i][t.dataset.k]=t.value;S.ack=false;S.downloaded=false;render();}
  });
  root.addEventListener('change',e=>{
    const t=e.target;
    if(t===el('ack')){S.ack=t.checked;render();}
    if(t.dataset.k==='amt'&&amount(S.rows[+t.dataset.i])){t.blur();render();}
  });
  root.addEventListener('focusout',e=>{if(e.target.dataset&&e.target.dataset.k)setTimeout(render,0);});
  root.addEventListener('click',e=>{
    const b=e.target.closest('button');if(!b)return;
    if(b.dataset.go){if(!b.disabled)go(+b.dataset.go);return;}
    if(b.dataset.tile){S.filter=b.dataset.tile;el('trows').innerHTML='';render();return;}
    if(b.dataset.comment){copy(b.dataset.comment,'송금 코멘트를');return;}
    const act=b.dataset.act;
    if(act==='restart'){reset();toast('처음부터 다시 시작해요');}
    else if(act==='paste'){el('thread').value=THREAD;render();}
    else if(act==='addThread'){
      if(S.threads>=3){toast('체험에서는 입력칸을 3개까지 추가할 수 있어요');return;}
      S.threads++;el('threads').insertAdjacentHTML('beforeend','<div class="m-rel m-extra"><div class="m-sub">이체 요청 '+S.threads+'</div><textarea class="m-input" rows="3" placeholder="다른 스레드가 있으면 여기에 붙여넣어요" aria-label="이체 요청 '+S.threads+'"></textarea></div>');
      toast('입력칸을 추가했어요');
    }
    else if(act==='extract'){
      if(!el('thread').value.trim()){const er=el('err1');er.textContent='이체 요청 1이 비어 있어요. Slack 스레드 내용을 붙여넣어 주세요.';er.hidden=false;el('thread').focus();return;}
      S.rows=BASE.map(r=>Object.assign({},r));S.filter='all';S.ack=false;S.downloaded=false;
      toast('이체정보 '+S.rows.length+'건을 추출했어요');go(2);
    }
    else if(act==='memo'){S.memo=el('memo').value.trim();toast(S.memo?'출금통장표시를 “'+S.memo+'”로 넣었어요':'출금통장표시에 차량번호를 넣어요');render();}
    else if(act==='next'){
      const err=S.rows.filter(r=>status(r)==='error').length;
      if(err){const er=el('err2');er.textContent='오류 '+err+'건을 먼저 고쳐 주세요.';er.hidden=false;return;}
      go(3);
    }
    else if(act==='download'){
      if(b.disabled)return;S.downloaded=true;
      const total=S.rows.reduce((a,r)=>a+amount(r),0);
      toast('실제 도구라면 '+S.rows.length+'건 · 총 '+won(total)+'원 이체파일(.xlsx)이 저장돼요. 체험이라 받지는 않아요.');render();
    }
  });
  reset();
}

document.addEventListener('DOMContentLoaded',()=>{
  const d=document.getElementById('dmock'),t=document.getElementById('tmock');
  if(d)dispatchDemo(d);if(t)transferDemo(t);
});
})();
