/* Real browser against the real temporary Python service; synthetic files only.
   INKBOUND_FIXTURE points to tests.serve_fixture's private runtime JSON. */
const {chromium}=require('playwright');
const fs=require('fs');
const assert=require('assert/strict');
(async()=>{
  const fixture=JSON.parse(fs.readFileSync(process.env.INKBOUND_FIXTURE,'utf8'));
  const browser=await chromium.launch({headless:true,executablePath:process.env.PLAYWRIGHT_CHROMIUM_PATH||chromium.executablePath()});
  const errors=[];
  try{
    for(const width of [390,768,1280]){
      const context=await browser.newContext({viewport:{width,height:900},deviceScaleFactor:1});
      const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
      await page.goto(fixture.base_url);
      await page.getByLabel('Email',{exact:true}).fill(fixture.browser_account);
      await page.getByLabel('Password',{exact:true}).fill(fixture.browser_password);
      await page.getByRole('button',{name:'Sign in',exact:true}).click();
      await page.locator('#workspace').waitFor({state:'visible'});
      await page.waitForFunction(()=>document.querySelector('#device-select').value.length>0);
      for(const name of ['Send documents','Readers','Tom','Privacy & settings']){
        await page.getByRole('button',{name,exact:true}).click();
        assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),`${name} overflows at${width}`);
      }
      await page.getByRole('button',{name:'Tom',exact:true}).click();
      assert.match(await page.locator('#tom-config').innerText(),/not configured/);
      assert(await page.locator('#tom-send').isDisabled());
      await page.locator('#question').fill(`Synthetic draft at ${width}`);
      await page.reload();
      await page.locator('#workspace').waitFor({state:'visible'});
      await page.getByRole('button',{name:'Tom',exact:true}).click();
      assert.equal(await page.locator('#question').inputValue(),`Synthetic draft at ${width}`);
      await page.getByRole('button',{name:'Send documents',exact:true}).click();
      await page.locator('#files').setInputFiles({name:`browser-${width}.txt`,mimeType:'text/plain',buffer:Buffer.from(`Synthetic browser delivery ${width}.`)});
      await page.waitForFunction((name)=>Array.from(document.querySelectorAll('#jobs .job')).some(j=>j.textContent.includes(name)&&j.textContent.includes('waiting for device')),`browser-${width}.txt`);
      const jobs=await page.locator('#jobs').innerText();assert(jobs.includes(`browser-${width}.txt`));
      await page.screenshot({path:`work/browser-${width}.png`,fullPage:true});
      await page.getByRole('button',{name:'Sign out',exact:true}).click();
      await page.locator('#signin').waitFor({state:'visible'});
      await context.close();
      console.log(`PASS ${width}px sign-in, four sections, no overflow, offline draft, real upload/conversion, sign-out`);
    }
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
