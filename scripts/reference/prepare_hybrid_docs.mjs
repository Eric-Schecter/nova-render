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
const scenes = fixture.scenes.map((scene) => ({
  id: scene.id,
  name: names[scene.id],
  assets: scene.assets.map(({ id, path, sourceSpace }) => {
    cpSync(path, directory + '/' + id + '.glb')
    return { id, url: id + '.glb', sourceSpace }
  }),
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
    'Stanford bunny: Stanford University Computer Graphics Laboratory.\n' +
    'Node transforms follow the pinned reference mesh importer; see Nova validation documentation.\n',
)
console.log('Documentation reference scenes prepared; excluded from SDK assets')
