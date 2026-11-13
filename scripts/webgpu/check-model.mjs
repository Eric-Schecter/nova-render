import {chromium} from 'playwright'
import {mkdir,readFile} from 'node:fs/promises'
await mkdir('build/webgpu',{recursive:true})
const browser=await chromium.launch({channel:'chrome',headless:true,args:['--enable-unsafe-webgpu']})
try {
  const page=await browser.newPage({viewport:{width:1400,height:1000}})
  const errors=[];page.on('pageerror',e=>errors.push(e.message))
  await page.goto('http://127.0.0.1:5173/examples/damaged-helmet')
  await page.locator('[data-ready="true"]').waitFor({timeout:45000})
  const target=page.locator('canvas')
  const before=await target.screenshot()
  await page.getByRole('slider',{name:'水平角',exact:true}).fill('65')
  // Wait for the actual rendered image, not just the input label to update.
  let changed=false
  for(let i=0;i<8;i++){if(!before.equals(await target.screenshot())){changed=true;break}}
  if(!changed)throw Error('Camera did not change GPU output')
  await page.getByRole('button',{name:'隐藏预览',exact:true}).click()
  if(await target.isVisible())throw Error('Preview did not hide')
  await page.getByRole('button',{name:'显示预览',exact:true}).click()
  await target.waitFor({state:'visible'})
  await page.getByRole('tab',{name:'C++ 示例',exact:true}).click()
  if(await page.getByRole('textbox').inputValue()!==await readFile('examples/helmet/main.cpp','utf8'))throw Error('C++ source mismatch')
  await page.getByRole('tab',{name:'HLSL shader',exact:true}).click()
  if(await page.getByRole('textbox').inputValue()!==await readFile('render/shaders/model.hlsl','utf8'))throw Error('HLSL source mismatch')
  await page.getByRole('tab',{name:'JavaScript 示例',exact:true}).click()
  await page.getByRole('button',{name:'重置相机',exact:true}).click()
  await page.screenshot({path:'build/webgpu/damaged-helmet-page.png',fullPage:true})
  await page.goto('http://127.0.0.1:5173/examples/triangle')
  await page.locator('[data-ready="true"]').waitFor({timeout:45000})
  if(errors.length)throw Error(errors.join('\n'))
  console.log('PASS model load, GPU camera output, hide/show, C++/HLSL source, disposal/navigation and triangle regression')
} finally {await browser.close()}
