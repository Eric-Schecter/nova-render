import { chromium } from 'playwright'
import { resolve } from 'node:path'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } }),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  for (const width of [1400, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    for (const slug of [
      'triangle',
      'damaged-helmet',
      'lighting',
      'environment',
      'skeletal-animation',
      'integrators',
    ]) {
      await page.goto('http://127.0.0.1:5173/examples/' + slug)
      await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
      const panel = page.getByRole('region', { name: '参数面板', exact: true }),
        canvas = page.locator('canvas')
      await panel.scrollIntoViewIfNeeded()
      const open = page.getByRole('button', { name: '显示参数面板', exact: true })
      if (await open.count()) await open.click()
      if (slug !== 'triangle') {
        const mode = panel.getByRole('combobox', { name: '渲染模式', exact: true })
        if (
          (await mode.inputValue()) !==
          (slug === 'integrators' ? 'integrator-comparison' : 'raster')
        )
          throw Error('Web default mode changed')
        if (
          slug !== 'skeletal-animation' &&
          (await mode.locator('option[value="ray-tracing"]').isDisabled())
        )
          throw Error('Web BVH path tracing should be selectable')
      }
      const a = await panel.boundingBox(),
        b = await canvas.boundingBox()
      if (slug === 'integrators') {
        const layout = await canvas.evaluate((e) => {
          const box = e.getBoundingClientRect()
          const legend = e.closest('.nova-viewport').querySelector('[aria-label="积分器对比结果"]')
          return {
            ratio: box.width / box.height,
            columns: getComputedStyle(legend).gridTemplateColumns.split(' ').length,
            rows: getComputedStyle(legend).gridTemplateRows.split(' ').length,
          }
        })
        if (Math.abs(layout.ratio - 1) > 0.01 || layout.columns !== 2 || layout.rows !== 2)
          throw Error('Comparison is not a 2x2 grid')
        if (a.x < b.x || a.y < b.y + b.height + 8 || a.x + a.width > b.x + b.width + 1)
          throw Error('Comparison controls overlap render surface')
      } else if (
        a.x < b.x ||
        a.y < b.y ||
        a.x + a.width > b.x + b.width + 1 ||
        a.y + a.height > b.y + b.height + 1
      ) {
        throw Error('Regular example overlay left the render area')
      }
      const controls = await page
        .locator('input[type=range],select')
        .evaluateAll((elements) => elements.every((e) => e.closest('.nova-panel')))
      if (!controls) throw Error('Parameter controls remain outside preview')
      await page.getByRole('button', { name: '收起参数面板', exact: true }).click()
      if (await panel.getByRole('combobox', { name: '背景色', exact: true }).isVisible())
        throw Error('Panel did not collapse')
      await page.getByRole('button', { name: '显示参数面板', exact: true }).click()
      await panel
        .getByRole('combobox', { name: '背景色', exact: true })
        .waitFor({ state: 'visible' })
      if (width === 1400 && slug === 'lighting') {
        const yaw = page.getByRole('slider', { name: '水平角', exact: true }),
          distance = page.getByRole('slider', { name: '距离', exact: true })
        const oldYaw = await yaw.inputValue(),
          oldDistance = await distance.inputValue()
        const red = page.getByRole('slider', { name: '光源红', exact: true })
        await red.scrollIntoViewIfNeeded()
        const r = await red.boundingBox()
        await page.mouse.move(r.x + r.width * 0.7, r.y + r.height / 2)
        await page.mouse.down()
        await page.mouse.move(r.x + r.width * 0.25, r.y + r.height / 2, { steps: 5 })
        await page.mouse.up()
        await page.mouse.wheel(0, 150)
        if ((await yaw.inputValue()) !== oldYaw || (await distance.inputValue()) !== oldDistance)
          throw Error('UI input moved the orbit camera')
        if (Number(await red.inputValue()) >= 0.9) throw Error('Real UI drag did not adjust light')
        await page.getByRole('button', { name: '重置光源', exact: true }).click()
        if (Number(await red.inputValue()) !== 1) throw Error('Reset light failed')
        await yaw.fill('60')
        await page.getByRole('button', { name: '重置相机', exact: true }).click()
        if (Number(await yaw.inputValue()) !== 20) throw Error('Reset camera failed')
        await canvas.scrollIntoViewIfNeeded()
        const c = await canvas.boundingBox()
        await page.mouse.move(c.x + 30, c.y + c.height * 0.5)
        await page.mouse.down()
        await page.mouse.move(c.x + 70, c.y + c.height * 0.5, { steps: 5 })
        await page.mouse.up()
        await page.waitForFunction(
          () => Number(document.querySelector('[aria-label="水平角"]').value) !== 20,
        )
      }
      await page.getByRole('button', { name: '隐藏预览', exact: true }).click()
      if (await panel.isVisible()) throw Error('Panel did not hide with preview')
      await page.getByRole('button', { name: '显示预览', exact: true }).click()
      await panel.waitFor({ state: 'visible' })
      if ((await page.evaluate(() => document.documentElement.scrollWidth)) > width)
        throw Error('Page overflow')
      if (slug === 'lighting' || slug === 'integrators')
        await page
          .locator('.nova-viewport')
          .screenshot({ path: `build/webgpu/panel-${slug}-${width}.png` })
    }
  }
  await page.evaluate(
    async (moduleURL) => {
      const Nova = await import(moduleURL),
        host = document.createElement('div'),
        canvas = document.createElement('canvas')
      canvas.style = 'width:400px;height:400px'
      host.append(canvas)
      document.body.append(host)
      const renderer = await Nova.createRenderer({ canvas })
      const first = renderer.enableParameterPanel()
      renderer.enableParameterPanel()
      first()
      if (host.querySelectorAll('.nova-panel').length !== 1)
        throw Error('Duplicate panel or stale cleanup removed active panel')
      renderer.disableParameterPanel()
      if (canvas.parentElement !== host || host.querySelector('.nova-panel'))
        throw Error('Disable did not restore canvas')
      renderer.enableParameterPanel()
      renderer.dispose()
      if (canvas.parentElement !== host || host.querySelector('.nova-panel'))
        throw Error('Dispose did not restore canvas')
      if (document.querySelectorAll('#nova-panel-style').length !== 1)
        throw Error('Panel stylesheet duplicated')
      host.remove()
    },
    '/@fs/' + resolve('web/src/Nova.js'),
  )
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS comparison controls below rendering, five original overlays, desktop/mobile bounds, collapse, real pointer/wheel isolation, scene drag, reset, hide/show and enable/disable/dispose',
  )
} finally {
  await browser.close()
}
