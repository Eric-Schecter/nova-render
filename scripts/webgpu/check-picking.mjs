import { chromium } from 'playwright'
import { readFileSync, writeFileSync, mkdirSync, realpathSync } from 'node:fs'
const base = process.env.NOVA_TEST_URL || 'http://127.0.0.1:5216'
const fixtures = JSON.parse(readFileSync('tests/fixtures/picking.json', 'utf8'))
const native = JSON.parse(readFileSync('build/picking-native.json', 'utf8'))
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
})
mkdirSync('build/validation/picking', { recursive: true })
try {
  const page = await browser.newPage(),
    errors = []
  page.on('pageerror', (e) => errors.push(e.message))
  await page.goto(base)
  const result = await page.evaluate(
    async ({ root, fixtures, native }) => {
      const { SceneData } = await import(root + '/web/src/SceneData.js')
      const { PickMesh } = await import(root + '/web/src/Picking.js')
      const { modelTriangleFloats, modelVertexFloats } = await import(root + '/web/src/ModelVertex.js')
      const { createRenderer } = await import(root + '/web/src/Nova.js')
      const require = (v, m) => {
          if (!v) throw Error(m)
        },
        close = (v, e) =>
          require(Math.abs(v - e) <=
            1e-5 * Math.max(1e-6, Math.abs(e)), `Picking mismatch ${v} / ${e}`)
      const scene = new SceneData(),
        results = []
      for (let k = 0; k < fixtures.length; k++) {
        const test = fixtures[k]
        await scene.update(
          [{ id: 'box', url: '/generated/SceneRed.glb', sourceSpace: test.sourceSpace }],
          test.instances,
        )
        let hit = null,
          rejected = false
        try {
          hit = scene.pick(test.ray)
        } catch {
          rejected = true
        }
        require(rejected === test.invalid, test.name + ': validation mismatch')
        if (rejected) {
          require(native[k] === 'invalid', 'Native validation mismatch')
          results.push('invalid')
          continue
        }
        for (const expected of [test.expected, native[k]]) {
          require(!!hit === !!expected, test.name + ': hit/miss mismatch')
          if (hit) {
            require(hit.id === expected.id, 'ID mismatch')
            close(hit.distance, expected.distance)
            for (let j = 0; j < 3; j++) {
              close(hit.position[j], expected.position[j])
              close(hit.normal[j], expected.normal[j])
            }
          }
        }
        results.push(hit)
      }
      const data = { vertices: new Float32Array(4096 * modelTriangleFloats) }
      for (let i = 0; i < 4096; i++)
        for (let j = 0; j < 3; j++)
          data.vertices.set(
            [
              [i * 3, 0, 0],
              [i * 3 + 1, 0, 0],
              [i * 3, 1, 0],
            ][j],
            i * modelTriangleFloats + j * modelVertexFloats,
          )
      const begin = performance.now(),
        mesh = new PickMesh(data)
      for (let i = 0; i < 4096; i += 17) {
        const h = mesh.pick([i * 3 + 0.2, 0.2, 5], [0, 0, -1], 100)
        require(h && h.triangle === i && h.distance === 5, 'BVH hit mismatch')
        require(!mesh.pick(
          [i * 3 + 0.9, 0.9, 5],
          [0, 0, -1],
          100,
        ), 'Picked AABB instead of triangle')
      }
      const bvhMilliseconds = performance.now() - begin
      const canvas = document.createElement('canvas')
      canvas.style = 'width:240px;height:180px'
      document.body.replaceChildren(canvas)
      const r = await createRenderer({ canvas, shaderDirectory: '/generated/' })
      r.setContinuousRendering(false)
      const origin = [0.2, 0.07, 5],
        direction = [0, 0, -1]
      require(r.pickScene({ origin, direction }) === null, 'Inactive renderer returned a stale hit')
      const assets = [{ id: 'box', url: '/generated/SceneRed.glb' }],
        instances = [{ id: 'box-instance', assetId: 'box' }]
      await r.setScene(assets, instances)
      const h = r.pickScene({
        origin: new Float32Array(origin),
        direction: new Float32Array(direction),
      })
      require(h?.id === 'box-instance', 'Public renderer API missing')
      close(h.distance, 4.5)
      h.normal[2] = 123
      h.position[0] = 123
      require(r.pickScene({ origin, direction }).normal[2] === 1, 'Returned result aliases state')
      // Filtering is a read-only query; it must not alter scene instances or the image.
      require(!r.pickScene({ origin, direction, excludeIds: ['box-instance'] }), 'Exclude failed')
      require(r.pickScene({ origin, direction }).id === 'box-instance', 'Filtering mutated scene')
      const invalid = [
        null,
        {},
        { origin, direction: [0, 0, 0] },
        { origin: [NaN, 0, 0], direction },
        { origin, direction: [0, 0, Infinity] },
        { origin, direction, includeIds: 'box-instance' },
        { origin, direction, excludeIds: [1] },
        { origin, direction, includeIds: null },
        { origin, direction, includeIds: [''] },
        { origin, direction: [, 0, -1] },
      ]
      for (const ray of invalid) {
        let rejected = false
        try {
          r.pickScene(ray)
        } catch {
          rejected = true
        }
        require(rejected, 'Invalid JS ray accepted')
      }
      try {
        await r.setScene(assets, [...instances, ...instances])
      } catch {}
      require(r.pickScene({ origin, direction })?.id ===
        'box-instance', 'Failed setScene replaced picking state')
      const moved = [{ ...instances[0], world: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3, 0, 0, 1] }]
      await r.setScene(assets, moved)
      require(!r.pickScene({ origin, direction }), 'Transform-only update left stale hits')
      await r.setScene([], [])
      require(!r.pickScene({ origin, direction }), 'Empty scene left stale hits')
      await r.setScene(assets, instances)
      await r.loadModel('/generated/SceneRed.glb')
      require(!r.pickScene({ origin, direction }), 'Legacy load retained scene hits')
      await r.setScene(assets, instances)
      await r.drawFrame()
      r.dispose()
      let disposedRejected = false
      try {
        r.pickScene({ origin, direction })
      } catch {
        disposedRejected = true
      }
      require(disposedRejected, 'Disposed renderer accepted picking')
      return { fixtures: results.length, bvhMilliseconds, results }
    },
    { root: '/@fs' + realpathSync('.'), fixtures, native },
  )
  if (errors.length) throw Error(errors.join('\n'))
  writeFileSync('build/validation/picking/web.json', JSON.stringify(result, null, 2))
  console.log(
    'PASS native/Web picking parity:',
    result.fixtures,
    'cases; 4096-triangle BVH:',
    result.bvhMilliseconds.toFixed(1),
    'ms',
  )
} finally {
  await browser.close()
}
