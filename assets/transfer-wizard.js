(()=>{
'use strict';
const frame=document.getElementById('transferWorkflowFrame');
const summary=document.getElementById('workflowStatus');
const notice=document.getElementById('workflowNotice');
const stage=document.body.dataset.stage;
const origin=window.location.origin;
const routes={input:'./transfer-input.html',review:'./transfer-review.html',download:'./transfer-download.html'};
let ready=false;
let resizeObserver=null;
let current={count:0,invalid:0,total:'0'};
function info(message,error=false){
 notice.textContent=message;
 summary.classList.toggle('error-note',error);
}
function showStatus(data){
 if(!data||typeof data.count!=='number')return;
 current={count:data.count,invalid:data.invalid,total:data.total};
 const label=data.count>0?'현재 '+data.count+'건 · 총 '+data.total+'원 · 검토 필요 '+data.invalid+'건':'현재 추출된 이체 내역이 없습니다.';
 info(label,stage==='download'&&data.invalid>0);
}
function fitFrame(){
 try{
  const child=frame.contentWindow;
  const main=child.document.querySelector('main');
  if(!main)return;
  const rect=main.getBoundingClientRect();
  const h=Math.ceil(rect.bottom+child.scrollY+24);
  frame.style.height=Math.max(410,h)+'px';
 }catch(error){/* Frame remains scrollable if resizing is restricted. */}
}
function navigate(href){
 if(!ready){info('이체파일 화면이 준비된 후 이동해 주세요.',true);return}
 try{
  const child=frame.contentWindow;
  if(typeof child.workflowSave!=='function'||!child.workflowSave()){
   info('작업 내용을 안전하게 저장하지 못했습니다. 현재 페이지에서 내용을 확인하세요.',true);
   return;
  }
  window.location.assign(href);
 }catch(error){
  info('단계 이동에 실패했습니다. 새로고침하지 말고 입력 내용을 확인하세요.',true);
 }
}
for(const a of document.querySelectorAll('[data-workflow-link]')){
 a.addEventListener('click',event=>{
  if(event.metaKey||event.ctrlKey||event.shiftKey||event.altKey||event.button!==0)return;
  const href=a.getAttribute('href');
  if(!href||!href.includes('.html'))return;
  event.preventDefault();
  if(href===routes[stage])return;
  navigate(href);
 });
}
const startOver=document.getElementById('workflowRestart');
if(startOver)startOver.addEventListener('click',()=>{
 if(!ready)return;
 if(!window.confirm('현재 이체 요청 원문과 편집 내역, 복원 기록을 모두 지우고 새 작업을 시작하시겠습니까?'))return;
 try{
  if(!frame.contentWindow.workflowResetAll()){info('초기화하지 못했습니다. 현재 내용을 확인하세요.',true);return}
  window.location.assign(routes.input);
 }catch(error){info('초기화에 실패했습니다. 현재 내역은 유지됩니다.',true)}
});
frame.addEventListener('load',()=>{
 ready=true;
 try{
  const child=frame.contentWindow;
  const state=child.workflowSummary?.();
  if(state)showStatus(state);
  const doc=child.document;
  if(resizeObserver)resizeObserver.disconnect();
  resizeObserver=new ResizeObserver(()=>requestAnimationFrame(fitFrame));
  resizeObserver.observe(doc.querySelector('main'));
  fitFrame();
 }catch(error){info('화면 연결이 지연되고 있습니다. 표시된 이체 정보를 확인하세요.',true)}
});
window.addEventListener('message',event=>{
 if(event.origin!==origin||event.source!==frame.contentWindow)return;
 if(event.data?.type==='worktool-transfer-status')showStatus(event.data);
 fitFrame();
});
window.addEventListener('resize',fitFrame);
})();