import { chromium } from 'playwright'
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
try {
  const page = await browser.newPage()
  const errors = []
  page.on('pageerror', (error) => errors.push(error.message))
  for (const width of [1400, 390]) {
    await page.setViewportSize({ width, height: 900 })
    for (const slug of ['triangle', 'damaged-helmet', 'lighting']) {
      await page.goto('http://127.0.0.1:5173/examples/' + slug)
      await page.locator('[data-ready="true"]').waitFor({ timeout: 45000 })
      const canvas = page.locator('canvas')
      await canvas.evaluate((element) => {
        element.dataset.expandIdentity = 'original'
      })
      for (const tab of ['JavaScript 示例', 'C++ 示例', 'HLSL shader']) {
        await page.getByRole('tab', { name: tab, exact: true }).click()
        const editor = page.getByRole('textbox')
        const source = await editor.inputValue()
        if (width === 1400) {
          const tabs = await page.getByRole('tablist').boundingBox()
          const button = await page
            .getByRole('button', { name: '全部展开', exact: true })
            .boundingBox()
          if (Math.abs(tabs.y - button.y) > 8) throw Error('Expand button is on a separate row')
        }
        await page.getByRole('button', { name: '全部展开', exact: true }).click()
        const collapse = page.getByRole('button', { name: '全部收起', exact: true })
        if ((await collapse.getAttribute('aria-expanded')) !== 'true')
          throw Error('Expansion state missing')
        const dimensions = await editor.evaluate((element) => ({
          client: element.clientHeight,
          scroll: element.scrollHeight,
        }))
        if (dimensions.scroll > dimensions.client + 1)
          throw Error(`Code still scrolls vertically: ${slug} ${tab}`)
        if ((await editor.inputValue()) !== source) throw Error('Expansion changed code')
        // Expanded state persists when switching language tabs.
        const otherTab = tab === 'C++ 示例' ? 'JavaScript 示例' : 'C++ 示例'
        await page.getByRole('tab', { name: otherTab, exact: true }).click()
        await collapse.waitFor({ state: 'visible' })
        await page.getByRole('tab', { name: tab, exact: true }).click()
        await collapse.click()
        if ((await page.locator('.hlsl-editor').evaluate((element) => element.clientHeight)) > 400)
          throw Error('Code did not collapse')
        if ((await editor.inputValue()) !== source) throw Error('Collapse changed code')
      }
      if ((await canvas.getAttribute('data-expand-identity')) !== 'original')
        throw Error('Expansion recreated preview')
      await page.getByRole('button', { name: '隐藏预览', exact: true }).click()
      await page.getByRole('button', { name: '全部展开', exact: true }).click()
      if (await canvas.isVisible()) throw Error('Code expansion changed preview visibility')
      await page.getByRole('button', { name: '全部收起', exact: true }).click()
      await page.getByRole('button', { name: '显示预览', exact: true }).click()
      if ((await page.evaluate(() => document.documentElement.scrollWidth)) > width)
        throw Error('Horizontal overflow')
    }
  }
  if (errors.length) throw Error(errors.join('\n'))
  console.log(
    'PASS all examples/languages on desktop/mobile: full code, collapse, persistent state/source/canvas and independent preview toggle',
  )
} finally {
  await browser.close()
}
