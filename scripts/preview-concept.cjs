const {chromium}=require('playwright-core');
const fs=require('node:fs');
const assert=require('node:assert/strict');
(async()=>{
const browser=await chromium.launch({executablePath:process.env.CHROME_BIN||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
try {
 const page=await browser.newPage({viewport:{width:1440,height:1080},deviceScaleFactor:1});
 page.on('dialog',d=>d.dismiss());
 await page.goto('http://127.0.0.1:8765/index.html',{waitUntil:'domcontentloaded'});
 await page.screenshot({path:'guide/theme-preview-home.png'});
 await page.goto('http://127.0.0.1:8765/dispatch.html',{waitUntil:'domcontentloaded'});
 await page.waitForTimeout(1700);
 const frame=page.frames().find(f=>f.url().includes('dispatch-tool.html'));
 assert.ok(frame,'dispatch iframe present');
 await frame.waitForSelector('#run');
 const style=await frame.evaluate(()=>({theme:!!document.querySelector('#worktool-concept-theme'),grid:getComputedStyle(document.querySelector('.workspace')).gridTemplateColumns,color:getComputedStyle(document.querySelector('#run')).backgroundColor}));
 assert.ok(style.theme,'iframe visual stylesheet should be injected');
 console.log('Dispatch preview CSS:',style);
 await page.screenshot({path:'guide/theme-preview-dispatch.png'});
 await frame.locator('#demo').click();
 await page.waitForTimeout(1100);
 await page.screenshot({path:'guide/theme-preview-dispatch-data.png'});
 await page.goto('http://127.0.0.1:8765/transfer.html',{waitUntil:'domcontentloaded'});
 await page.screenshot({path:'guide/theme-preview-transfer.png'});
 const input=page.locator('#raw');
 await input.fill('123가4567\n국민은행\n123-456-789012\n35,000원');
 await page.locator('#parse').click();
 await page.locator('#rows tr').first().waitFor({state:'visible',timeout:15000});
 assert.equal(await page.locator('#rows tr').count(),1);
 await page.locator('#stepReview').screenshot({path:'guide/theme-preview-transfer-review.png'});
 await page.locator('#stepDownload').screenshot({path:'guide/theme-preview-transfer-download.png'});
 const grid=await page.locator('.input-grid').evaluate(el=>getComputedStyle(el).gridTemplateColumns);
 console.log('Transfer input grid:',grid);
} finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1});
