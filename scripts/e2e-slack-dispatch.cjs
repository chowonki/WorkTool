const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright-core');

const root = path.resolve(__dirname, '..');
const fixtures = path.join(__dirname, 'fixtures');
const base = process.env.BASE_URL || 'http://worktool.test';

async function run() {
 const browser = await chromium.launch({ executablePath: process.env.CHROME_BIN });
 try {
  const context = await browser.newContext({ viewport: { width: 1366, height: 900 }, acceptDownloads: true });
  if (!process.env.BASE_URL) await context.route(base + '/**', route => {
   const relative = new URL(route.request().url()).pathname.slice(1);
   const file = path.resolve(root, relative);
   return file.startsWith(root + path.sep) && fs.existsSync(file) && fs.statSync(file).isFile()
    ? route.fulfill({ path: file })
    : route.fulfill({ status: 404 });
  });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(base + '/dispatch.html');
  await page.locator('#input').fill(fs.readFileSync(path.join(fixtures, '차량목록.txt'), 'utf8'));
  await page.locator('#fileInput').setInputFiles(['한빛탁송_가상.xlsx', '오픈마일_가상.xlsx', '렌톡_가상.xlsx'].map(file => path.join(fixtures, file)));
  await page.waitForFunction(() => document.querySelectorAll('#fileList li').length === 3);
  await page.locator('#run').click();
  await page.waitForFunction(() => document.querySelectorAll('#body tr').length === 10);
  await page.locator('#vendorFilter').selectOption('한빛탁송');
  await page.locator('#openSlackReplies').click();
  await page.locator('#slackRepliesInput').fill('08구8701 - 12.350원\n77다1234 - 확인 중');
  await page.getByRole('button', { name: '확인 내용 반영' }).click();
  await page.locator('#slackRepliesDialog').waitFor({ state: 'hidden' });
  assert.deepEqual(await page.locator('#body .fuel').allTextContents(), ['12,350', '확인 중']);
  assert.equal(await page.locator('#body .fuel').first().getAttribute('data-copy'), '12350');
  const [download] = await Promise.all([page.waitForEvent('download'), page.locator('[data-export="txt"]').click()]);
  assert.match(fs.readFileSync(await download.path(), 'utf8'), /77다1234\t확인 중/);
  await page.locator('#openSlackReplies').click();
  await page.locator('#slackRepliesInput').fill('08구8701 - X\n77다1234 - 12,34원');
  await page.getByRole('button', { name: '확인 내용 반영' }).click();
  assert.match(await page.locator('#slackRepliesReport').textContent(), /1대 반영 · 확인 필요 1건/);
  await page.locator('#viewSlackReplies').click();
  assert.deepEqual(await page.locator('#body .fuel').allTextContents(), ['0', '확인 중']);
  assert.deepEqual(errors, []);
  if (process.env.WORKTOOL_SCREENSHOT) await page.screenshot({ path: process.env.WORKTOOL_SCREENSHOT });
  console.log('PASS Slack replies merge into the browser table and TXT export');
 } finally {
  await browser.close();
 }
}

run().catch(error => { console.error(error); process.exitCode = 1; });
