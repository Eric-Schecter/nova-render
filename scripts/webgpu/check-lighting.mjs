import { chromium } from 'playwright'
import { mkdir, readFile } from 'node:fs/promises'
import { resolve } from 'node:path'
await mkdir('build/webgpu', { recursive: true })
const browser = await chromium.launch({channel:'chrome',headless:true,args:['--enable-unsafe-webgpu']})
try {
  const page = await browser.newPage({viewport:{width:1400,height:1000}})
  const errors=[];page.on('pageerror',e=>errors.push(e.message))
  for (const lighting of [false,true]) {
    await page.goto('http://127.0.0.1:5173/examples/'+(lighting?'lighting':'damaged-helmet'))
    await page.getByRole('status').filter({hasText:lighting?'PBR 光照':'基础颜色'}).waitFor({timeout:45000})
    if (await page.getByRole('slider',{name:'曝光',exact:true}).count() !== Number(lighting)) throw Error('Exposure control must only appear in lighting example')
    await page.getByRole('tab',{name:'C++ 示例',exact:true}).click()
    if (await page.getByRole('textbox').inputValue()!==await readFile(`examples/${lighting?'lighting':'helmet'}/main.cpp`,'utf8')) throw Error('Wrong native example source')
    await page.getByRole('tab',{name:'JavaScript 示例',exact:true}).click()
    if (await page.getByRole('textbox').inputValue()!==await readFile(`web/examples/${lighting?'lighting':'helmet'}.js`,'utf8')) throw Error('Wrong web example source')
    await page.locator('canvas').screenshot({path:`build/webgpu/${lighting?'lighting':'base-color'}.png`})
  }
  // Compare the same renderer, model, camera and canvas pixels to isolate mode changes.
  await page.evaluate(async path=>{
    const Nova=await import('/@fs/'+path+'/web/src/Nova.js')
    const canvas=document.createElement('canvas');canvas.id='mode-check';canvas.style='width:400px;height:400px;position:fixed;top:0;left:0;z-index:9999';document.body.append(canvas)
    window.modeRenderer=await Nova.createRenderer({canvas})
    await modeRenderer.loadModel('/generated/DamagedHelmet.glb')
    modeRenderer.setCamera({yaw:20,pitch:5,exposure:1});await modeRenderer.drawFrame()
  },resolve('.'))
  const canvas=page.locator('#mode-check'),base=await canvas.screenshot()
  await page.evaluate(async()=>{modeRenderer.setCamera({exposure:3});await modeRenderer.drawFrame()})
  if(!base.equals(await canvas.screenshot())) throw Error('Base color was affected by exposure')
  await page.evaluate(async()=>{modeRenderer.setCamera({exposure:1});modeRenderer.setLighting('pbr');await modeRenderer.drawFrame()})
  const lit=await canvas.screenshot()
  if(base.equals(lit)) throw Error('PBR did not change pixels')
  await page.evaluate(async()=>{modeRenderer.setCamera({exposure:3});await modeRenderer.drawFrame()})
  if(lit.equals(await canvas.screenshot())) throw Error('PBR ignored exposure')
  await page.evaluate(async()=>{modeRenderer.setLighting('unlit');await modeRenderer.drawFrame()})
  if(!base.equals(await canvas.screenshot())) throw Error('Restoring unlit did not restore base color')
  await page.evaluate(()=>{let rejected=false;try{modeRenderer.setLighting('invalid')}catch{rejected=true}modeRenderer.dispose();if(!rejected)throw Error('Invalid lighting mode accepted')})
  if(errors.length)throw Error(errors.join('\n'))
  console.log('PASS separate example sources, exposure UI, unlit exposure independence, PBR pixels/exposure, runtime mode switch and invalid mode rejection')
} finally {await browser.close()}
