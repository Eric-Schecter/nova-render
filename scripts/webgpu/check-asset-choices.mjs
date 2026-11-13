import { chromium } from 'playwright'
import { createHash } from 'node:crypto'
import { resolve } from 'node:path'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1100 } }),
    errors = []
  page.on('pageerror', (error) => errors.push(error.message))
  const hash = async () =>
    createHash('sha256')
      .update(await page.locator('canvas').screenshot())
      .digest('hex')
  async function choose(select, value) {
    await select.selectOption(value)
    await page.waitForFunction(() =>
      [...document.querySelectorAll('select[aria-busy]')].every(
        (e) => e.getAttribute('aria-busy') === 'false',
      ),
    )
    await page.evaluate(
      () => new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve))),
    )
  }
  for (const slug of ['damaged-helmet', 'lighting', 'environment']) {
    await page.goto('http://127.0.0.1:5173/examples/' + slug)
    await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
    const models = page.getByRole('combobox', { name: '模型选择', exact: true }),
      initial = await hash(),
      hashes = new Set()
    if ((await models.locator('option').count()) !== 3) throw Error('Missing model choices')
    for (const name of ['Avocado', 'BoomBox', 'DamagedHelmet']) {
      await choose(models, '/generated/' + name + '.glb')
      hashes.add(await hash())
      if ((await page.getByRole('slider', { name: '水平角', exact: true }).inputValue()) !== '20')
        throw Error('Camera reset on model switch')
    }
    if (hashes.size !== 3 || (await hash()) !== initial)
      throw Error('Model choices did not change/restore GPU pixels')
    if (slug === 'environment') {
      const maps = page.getByRole('combobox', { name: '环境贴图', exact: true })
      if ((await maps.locator('option').count()) !== 3) throw Error('Missing environment choices')
      for (const model of ['Avocado', 'BoomBox', 'DamagedHelmet']) {
        await choose(models, '/generated/' + model + '.glb')
        const outputs = new Set()
        for (const name of ['kloppenheim_06', 'studio_small_09', 'venice_sunset']) {
          await choose(maps, '/generated/' + name + '.bin')
          outputs.add(await hash())
        }
        if (outputs.size !== 3) throw Error('Environment choices did not change GPU output')
      }
      await choose(models, '/generated/Avocado.glb')
      await choose(maps, '/generated/studio_small_09.bin')
      await page
        .locator('.nova-viewport')
        .screenshot({ path: 'build/webgpu/asset-choices-desktop.png' })
      await page.setViewportSize({ width: 390, height: 844 })
      if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth))
        throw Error('Mobile overflow')
      await page
        .locator('.nova-viewport')
        .screenshot({ path: 'build/webgpu/asset-choices-mobile.png' })
    }
  }
  // The UI must roll back its selection after a failed load and recover on the next choice.
  await page.setViewportSize({ width: 1400, height: 1100 })
  await page.evaluate(
    async (moduleURL) => {
      const Nova = await import(moduleURL)
      const host = document.createElement('div'),
        canvas = document.createElement('canvas')
      canvas.style = 'width:800px;height:600px'
      host.append(canvas)
      document.body.append(host)
      const renderer = await Nova.createRenderer({ canvas })
      await renderer.loadModel('/generated/Avocado.glb')
      await renderer.drawFrame()
      window.choiceTest = { renderer, host, errors: [] }
      renderer.enableParameterPanel({
        models: [
          { name: 'Avocado', url: '/generated/Avocado.glb' },
          { name: 'Missing', url: '/generated/missing.glb' },
          { name: 'BoomBox', url: '/generated/BoomBox.glb' },
        ],
        onError: (e) => window.choiceTest.errors.push(e.message),
      })
    },
    '/@fs' + resolve('web/src/Nova.js'),
  )
  const extra = page.locator('.nova-viewport').last(),
    select = extra.getByRole('combobox', { name: '模型选择', exact: true })
  await choose(select, '/generated/missing.glb')
  if ((await select.inputValue()) !== '/generated/Avocado.glb')
    throw Error('Failed choice did not roll back')
  if ((await page.evaluate(() => window.choiceTest.errors.length)) !== 1)
    throw Error('Missing UI error report')
  await choose(select, '/generated/BoomBox.glb')
  await page.evaluate(() => {
    window.choiceTest.renderer.dispose()
    window.choiceTest.host.remove()
  })
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS three model pages, nine model/environment combinations, GPU pixels, restored state, failed-choice recovery and mobile layout',
  )
} finally {
  await browser.close()
}
