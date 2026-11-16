export async function openExampleParameters(page) {
  const drawer = page.locator('.aa-drawer')
  if (await drawer.count()) {
    if (!(await drawer.evaluate((el) => el.open))) await drawer.locator('summary').click()
  } else {
    const button = page.getByRole('button', { name: 'Show parameters', exact: true })
    if (await button.count()) await button.click()
  }
}
export async function assertFloatingParameters(page, canvas, bar) {
  const control = page.locator('.aa-drawer,.ao-controls'),
    c = await canvas.boundingBox(),
    u = await control.boundingBox(),
    a = await bar.boundingBox()
  if (
    a.y + a.height > c.y + 1 ||
    u.y < c.y ||
    u.x < c.x - 1 ||
    u.x + u.width > c.x + c.width + 1 ||
    u.y + u.height > c.y + c.height + 1
  )
    throw Error('Floating controls or performance strip outside viewport')
}
