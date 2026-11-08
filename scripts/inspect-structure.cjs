const {chromium}=require('playwright-core');
(async()=>{
const browser=await chromium.launch({executablePath:process.env.CHROME_BIN||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
try{
const page=await browser.newPage({viewport:{width:1440,height:1080}});
await page.goto('http://127.0.0.1:8765/dispatch.html',{waitUntil:'domcontentloaded'});
await page.waitForTimeout(3000);
const frame=page.frames().find(f=>/dispatch-tool/.test(f.url()));
if(!frame)throw Error('iframe not loaded');
const meta=await frame.evaluate(()=>{
const ids=['body','input','files','demo','upload','results','result','file','run','lookup','search'];
const preview={};for(const id of ids){const el=document.getElementById(id);if(el){const a=[];for(let p=el,n=0;p&&n<5;p=p.parentElement,n++)a.push({tag:p.tagName,id:p.id,cls:p.className,role:p.getAttribute('role'),html:p.outerHTML.slice(0,n===1?1300:450)});preview[id]=a;}}
return{titles:[...document.querySelectorAll('h1,h2,h3')].slice(0,35).map(e=>({text:e.textContent.trim().slice(0,65),parent:e.parentElement?.className})),buttons:[...document.querySelectorAll('button')].slice(0,25).map(e=>({id:e.id,txt:e.textContent.trim().slice(0,55),class:e.className})),cards:[...document.querySelectorAll('section,article,.card,.panel')].slice(0,30).map(e=>({tag:e.tagName,id:e.id,cls:e.className,text:e.textContent.trim().slice(0,110)})),preview};
});
console.log('DISPATCH_STRUCTURE_JSON_START');
console.log(JSON.stringify(meta));
console.log('DISPATCH_STRUCTURE_JSON_END');
}finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
