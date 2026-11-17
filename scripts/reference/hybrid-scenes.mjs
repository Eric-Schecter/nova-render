// Application-owned reference assembly. Models remain in the ignored pinned-archive cache.
import { readFileSync, writeFileSync } from 'node:fs'
import { resolve, join } from 'node:path'
const cache = resolve('build/reference/hybrid-rendering'),
  manifest = JSON.parse(readFileSync('tests/fixtures/hybrid-reference/manifest.json'))
function glbBounds(name) {
  const b = readFileSync(join(cache, 'models', name + '.glb')),
    j = JSON.parse(b.toString('utf8', 20, 20 + b.readUInt32LE(12)))
  const mins = [Infinity, Infinity, Infinity],
    maxs = [-Infinity, -Infinity, -Infinity]
  for (const m of j.meshes)
    for (const p of m.primitives) {
      const a = j.accessors[p.attributes.POSITION]
      for (let k = 0; k < 3; k++) {
        mins[k] = Math.min(mins[k], a.min[k])
        maxs[k] = Math.max(maxs[k], a.max[k])
      }
    }
  return { minimum: mins, maximum: maxs }
}
const world = (scale = 1, x = 0, y = 0, z = 0, angle = 0) => {
  const c = Math.cos(angle) * scale,
    s = Math.sin(angle) * scale
  return [c, 0, -s, 0, 0, scale, 0, 0, s, 0, c, 0, x, y, z, 1]
}
const scenes = manifest.scenes.slice(0, 3).map((source) => {
  const names =
    source.id === 'shadows-test'
      ? ['pillar', 'bunny', 'ground']
      : [source.id === 'reflections-test' ? 'reflections_test' : 'global_illumination_test']
  const assets = names.map((id) => ({
      id,
      path: join(cache, 'models', id + '.glb'),
      url: '/@fs' + join(cache, 'models', id + '.glb'),
      sourceSpace: true,
    })),
    instances = []
  if (source.id === 'shadows-test') {
    const bounds = glbBounds('ground'),
      a = source.assembly
    for (const x of a.pillarRowsX)
      for (let i = 0; i < a.pillarsPerRow; i++)
        instances.push({
          id: `pillar-${x}-${i}`,
          assetId: 'pillar',
          world: world(
            1,
            x,
            0,
            bounds.minimum[2] +
              ((bounds.maximum[2] - bounds.minimum[2]) * (i + 1)) / (a.pillarsPerRow + 1),
          ),
        })
    instances.push(
      {
        id: 'bunny',
        assetId: 'bunny',
        world: world(
          a.bunny.scale,
          ...a.bunny.position,
          (a.bunny.rotationYDegrees * Math.PI) / 180,
        ),
      },
      { id: 'ground', assetId: 'ground', world: world() },
    )
  } else instances.push({ id: names[0], assetId: names[0], world: world(source.worldScale) })
  if (instances.length !== source.instances) throw Error('Reference instance count changed')
  return {
    id: source.id,
    triangles: source.assembledTriangles,
    assets,
    instances,
    cameras: source.cameras.map((c) => ({
      id: c.id,
      position: c.position,
      orientation: c.novaOrientationXYZW,
      projection: 'perspective',
      verticalFov: manifest.projection.verticalFovDegrees,
      nearPlane: manifest.projection.nearPlane,
      farPlane: manifest.projection.farPlane,
      exposure: 1,
    })),
  }
})
const effects = process.argv.includes('--effects')
const output = join(cache, effects ? 'scenes-effects.json' : 'scenes.json')
writeFileSync(
  output,
  JSON.stringify(
    {
      profile:
        'reference-import; shared hard directional light, no HDR; ' +
        (effects
          ? 'original geometry/materials/cameras; Hybrid effect validation, not reference radiance fidelity'
          : 'framing/visibility baseline only'),
      width: effects ? 640 : 320,
      height: effects ? 360 : 180,
      ...(effects
        ? { hybridEffects: { aoRadius: 2, aoSamples: 4, reflectionSamples: 1, historyFrames: 16 } }
        : {}),
      light: { direction: [-0.6, 0.9, 0.5], color: [1, 1, 1], intensity: 4 },
      scenes,
    },
    null,
    2,
  ) + '\n',
)
console.log(output)
