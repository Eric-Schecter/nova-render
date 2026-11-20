// Documentation assets only. SDK packagers never consume this directory.
import { execFileSync } from 'node:child_process'
import { cpSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { resolve } from 'node:path'
execFileSync('python3', ['scripts/reference/prepare_hybrid.py', '--download'], { stdio: 'inherit' })
execFileSync(process.execPath, ['scripts/reference/hybrid-scenes.mjs'], { stdio: 'inherit' })
const cache = resolve('build/reference/hybrid-rendering'),
  directory = 'docs/public/hybrid-reference',
  fixture = JSON.parse(readFileSync(cache + '/scenes.json')),
  names = {
    'shadows-test': 'Reference · Shadows Test',
    'reflections-test': 'Reference · Reflections Test',
    'global-illumination-test': 'Reference · Global Illumination Test',
  }
mkdirSync(directory, { recursive: true })
const reflectionSky = 'reflection-sky.nenv'
for (const file of [reflectionSky, 'SKY-LICENSE.txt']) {
  cpSync('tests/fixtures/hybrid-reference/' + file, directory + '/' + file)
}
const scenes = fixture.scenes.map((scene) => ({
  id: scene.id,
  name: names[scene.id],
  assets: scene.assets.map(({ id, path, sourceSpace }) => {
    cpSync(path, directory + '/' + id + '.glb')
    return { id, url: id + '.glb', sourceSpace }
  }),
  ...(scene.id === 'reflections-test'
    ? {
        environment: reflectionSky,
        light: { direction: [0.4967318, 0.5, -0.7094065], color: [1, 1, 1], intensity: 1 },
      }
    : {}),
  instances: scene.instances,
  cameras: scene.cameras,
}))
writeFileSync(directory + '/scenes.json', JSON.stringify({ light: fixture.light, scenes }))
cpSync(
  'tests/fixtures/hybrid-reference/reference.LICENSE.txt',
  directory + '/REFERENCE-LICENSE.txt',
)
writeFileSync(
  directory + '/SOURCE.txt',
  'Scenes converted from diharaw/hybrid-rendering 1.1.0.\n' +
    'Source: https://github.com/diharaw/hybrid-rendering\n' +
    'Archive SHA-256: a73a1df3e6a4d8dc3eba07467e00e6e8aaf83758995486760095598f1f297f92\n' +
    'Code license: REFERENCE-LICENSE.txt. Models and textures retain their upstream authorship.\n' +
    'Reflection sky: Hosek–Wilkie RGB model evaluated with the reference dwSampleFramework defaults; see SKY-LICENSE.txt.\n' +
    'Coefficient source: https://github.com/diharaw/dwSampleFramework/tree/be5c6230ee85092cdd966056675c7f797a739ae6/extras\n' +
    'Stanford bunny: Stanford University Computer Graphics Laboratory.\n' +
    'Node transforms follow the pinned reference mesh importer; see Nova validation documentation.\n',
)
console.log('Documentation reference scenes prepared; excluded from SDK assets')
