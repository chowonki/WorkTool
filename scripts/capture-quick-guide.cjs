// Capture browser-rendered screenshots with safe, fictional sample values.
const {chromium}=require('playwright-core');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROME_BIN||'/usr/bin/google-chrome',headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
 try{
  const page=await browser.newPage({viewport:{width:1366,height:1000},deviceScaleFactor:1});
  page.on('dialog',dialog=>dialog.dismiss());
  await page.goto('http://127.0.0.1:8765/dispatch.html',{waitUntil:'domcontentloaded'});
  await page.waitForTimeout(2400);
  await page.screenshot({path:'guide/dispatch-screen.png',animations:'disabled'});
  await page.goto('http://127.0.0.1:8765/transfer.html',{waitUntil:'domcontentloaded'});
  await page.screenshot({path:'guide/transfer-screen.png',animations:'disabled'});
  await page.locator('#raw').fill('123가4567\n국민은행\n123-456-789012\n35,000원');
  await page.locator('#parse').click();
  await page.locator('#rows tr').first().waitFor({state:'visible',timeout:10000});
  await page.locator('#stepReview').screenshot({path:'guide/transfer-Review.png',animations:'disabled'});
  await page.locator('#stepDownload').screenshot({path:'guide/transfer-Download.png',animations:'disabled'});
  console.log('Captured real WorkTool UI screens, using placeholder sample data only.');
 }finally{await browser.close()}
})().catch(error=>{console.error(error);process.exitCode=1});
