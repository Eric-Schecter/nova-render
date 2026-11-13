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
    await page.locator('[data-ready="true"]').waitFor({timeout:45000})
    if (await page.getByRole('slider',{name:'曝光',exact:true}).count() !== Number(lighting)) throw Error('Exposure control must only appear in lighting example')
    await page.getByRole('tab',{name:'C++ 示例',exact:true}).click()
    if (await page.getByRole('textbox').inputValue()!==await readFile(`examples/${lighting?'lighting':'helmet'}/main.cpp`,'utf8')) throw Error('Wrong native example source')
    await page.getByRole('tab',{name:'JavaScript 示例',exact:true}).click()
    if (await page.getByRole('textbox').inputValue()!==await readFile(`web/examples/${lighting?'lighting':'helmet'}.js`,'utf8')) throw Error('Wrong web example source')
    if(await page.getByRole('status').count()) throw Error('Success status must not be displayed')
    if(lighting){
      let pixels=await page.locator('canvas').screenshot()
      for(const [name,value] of [['光源方位','100'],['光源仰角','-30'],['光源红','0.1'],['光源绿','0.1'],['光源蓝','0.1'],['光源强度','0']]){
        await page.getByRole('slider',{name,exact:true}).fill(value)
        let changed=false
        for(let i=0;i<10;i++){const next=await page.locator('canvas').screenshot();if(!pixels.equals(next)){pixels=next;changed=true;break}}
        if(!changed)throw Error('Light control did not affect pixels: '+name)
      }
      await page.getByRole('button',{name:'重置光源',exact:true}).click()
      await page.waitForFunction(()=>Number(document.querySelector('[aria-label="光源强度"]').value)===3.5)
      await page.getByRole('button',{name:'隐藏预览',exact:true}).click()
      await page.getByRole('slider',{name:'光源强度',exact:true}).fill('10')
      await page.getByRole('button',{name:'显示预览',exact:true}).click()
      await page.getByRole('button',{name:'重置光源',exact:true}).click()
      await page.setViewportSize({width:390,height:844})
      if(await page.evaluate(()=>document.documentElement.scrollWidth)>390)throw Error('Lighting controls overflow on mobile')
      await page.setViewportSize({width:1400,height:1000})
    }
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
  // Light settings leave base-color output intact, with copies preventing external mutation.
  await page.evaluate(async()=>{
    const original=modeRenderer.getDirectionalLight();window.savedLight=original
    const next={direction:[3,0,1],color:[0.2,0.7,1],intensity:12}
    modeRenderer.setDirectionalLight(next);next.color[0]=1
    const copy=modeRenderer.getDirectionalLight();copy.direction[0]=99
    if(modeRenderer.getDirectionalLight().color[0]!==0.2 || modeRenderer.getDirectionalLight().direction[0]!==3)throw Error('Light is not copied')
    await modeRenderer.drawFrame()
  })
  if(!base.equals(await canvas.screenshot())) throw Error('Light parameters affected unlit')
  await page.evaluate(async()=>{modeRenderer.setLighting('pbr');await modeRenderer.drawFrame()})
  const custom=await canvas.screenshot()
  await page.evaluate(async()=>{modeRenderer.setDirectionalLight(savedLight);await modeRenderer.drawFrame()})
  if(custom.equals(await canvas.screenshot()))throw Error('Custom API light did not change GPU output')
  await page.evaluate(()=>{
    const original=modeRenderer.getDirectionalLight()
    const invalid=[{...original,direction:[0,0,0]},{...original,direction:[Infinity,1,2]},{...original,color:[1,-1,0]},{...original,color:[1,2,0]},{...original,intensity:-1},{...original,intensity:101},{...original,intensity:NaN}]
    for(const value of invalid){let rejected=false;try{modeRenderer.setDirectionalLight(value)}catch{rejected=true}if(!rejected)throw Error('Invalid light accepted')}
    if(JSON.stringify(original)!==JSON.stringify(modeRenderer.getDirectionalLight()))throw Error('Invalid light changed state')
    let rejected=false;try{modeRenderer.setLighting('invalid')}catch{rejected=true}modeRenderer.dispose();if(!rejected)throw Error('Invalid lighting mode accepted')
  })
  if(errors.length)throw Error(errors.join('\n'))
  console.log('PASS separate example sources, exposure UI, unlit exposure independence, PBR pixels/exposure, runtime mode switch, directional light controls/copies/validation and no success status')
} finally {await browser.close()}
