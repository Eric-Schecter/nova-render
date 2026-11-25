// Convert the static Bistro reference GLB into Nova scene assets and instances.
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import { multiply, nodeMatrix } from '../../web/src/ModelData.js'

const [source, output] = process.argv.slice(2)
if (!source || !output)
  throw Error('Usage: node prepare-bistro-scene.mjs <Bistro.glb> <output-directory>')

const glb = await readFile(source)
const jsonSize = glb.readUInt32LE(12)
if (
  glb.toString('ascii', 0, 4) !== 'glTF' ||
  glb.readUInt32LE(4) !== 2 ||
  glb.toString('ascii', 16, 20) !== 'JSON' ||
  glb.readUInt32LE(24 + jsonSize) !== 0x004e4942
)
  throw Error('Expected a GLB with one embedded binary buffer')
const input = JSON.parse(glb.subarray(20, 20 + jsonSize))
const binary = glb.subarray(28 + jsonSize)

const identity = nodeMatrix({})
const references = new Map()
function visit(index, parent) {
  const node = input.nodes[index]
  const world = multiply(parent, nodeMatrix(node))
  if (node.mesh !== undefined) {
    if (node.skin !== undefined || node.extensions?.EXT_mesh_gpu_instancing)
      throw Error('Bistro validation expects static mesh nodes in bind pose')
    if (!references.has(node.mesh)) references.set(node.mesh, [])
    references.get(node.mesh).push(world)
  }
  for (const child of node.children ?? []) visit(child, world)
}
for (const root of input.scenes[input.scene ?? 0].nodes) visit(root, identity)

const groups = new Map()
let uniqueVertices = 0,
  expandedVertices = 0
for (const [mesh, transforms] of references) {
  const [primitive, ...rest] = input.meshes[mesh].primitives
  if (!primitive || rest.length || primitive.targets || (primitive.mode ?? 4) !== 4)
    throw Error('Bistro validation expects one triangle primitive per mesh')
  const count = input.accessors[primitive.indices ?? primitive.attributes.POSITION].count
  uniqueVertices += count
  expandedVertices += count * transforms.length
  // Batch one-off meshes by material. Repeated meshes stay in local space.
  const key = transforms.length > 1 ? 'mesh-' + mesh : 'material-' + primitive.material
  if (!groups.has(key))
    groups.set(key, { meshes: [], instances: transforms.length > 1 ? transforms : [identity] })
  groups
    .get(key)
    .meshes.push({ primitive, world: transforms.length > 1 ? identity : transforms[0] })
}

function buildAsset(meshes) {
  const doc = {
    asset: { version: '2.0' },
    extensionsUsed: input.extensionsUsed,
    scene: 0,
    scenes: [{ nodes: meshes.map((_, i) => i) }],
    nodes: [],
    meshes: [],
    buffers: [],
    bufferViews: [],
    accessors: [],
    materials: [],
    textures: [],
    images: [],
    samplers: [],
  }
  let byteLength = 0
  const chunks = [],
    remaps = new Map()
  function intern(table, index, copy) {
    if (!remaps.has(table)) remaps.set(table, new Map())
    const remap = remaps.get(table)
    if (!remap.has(index)) {
      const value = copy(structuredClone(input[table][index]))
      remap.set(index, doc[table].push(value) - 1)
    }
    return remap.get(index)
  }
  const view = (index) =>
    intern('bufferViews', index, (v) => {
      if (v.buffer !== 0) throw Error('Expected embedded buffer views')
      const start = v.byteOffset ?? 0
      const bytes = binary.subarray(start, start + v.byteLength)
      if (bytes.length !== v.byteLength) throw Error('Invalid buffer view')
      const padding = (4 - (byteLength % 4)) % 4
      if (padding) chunks.push(Buffer.alloc(padding))
      v.byteOffset = byteLength + padding
      byteLength = v.byteOffset + bytes.length
      chunks.push(bytes)
      return v
    })
  const accessor = (index) =>
    intern('accessors', index, (a) => {
      if (a.bufferView !== undefined) a.bufferView = view(a.bufferView)
      if (a.sparse) {
        a.sparse.indices.bufferView = view(a.sparse.indices.bufferView)
        a.sparse.values.bufferView = view(a.sparse.values.bufferView)
      }
      return a
    })
  const texture = (index) =>
    intern('textures', index, (t) => {
      if (t.extensions) throw Error('Expected core glTF textures')
      t.source = intern('images', t.source, (image) => {
        if (image.bufferView === undefined) throw Error('Expected embedded images')
        image.bufferView = view(image.bufferView)
        return image
      })
      if (t.sampler !== undefined) t.sampler = intern('samplers', t.sampler, (s) => s)
      return t
    })
  function remapTextures(record) {
    for (const [key, value] of Object.entries(record)) {
      if (key.endsWith('Texture') && Number.isInteger(value?.index))
        value.index = texture(value.index)
      else if (value && typeof value === 'object' && !Array.isArray(value)) remapTextures(value)
    }
    return record
  }
  for (const { primitive, world } of meshes) {
    const p = structuredClone(primitive)
    for (const key of Object.keys(p.attributes)) p.attributes[key] = accessor(p.attributes[key])
    if (p.indices !== undefined) p.indices = accessor(p.indices)
    if (p.material !== undefined) p.material = intern('materials', p.material, remapTextures)
    doc.nodes.push({ mesh: doc.meshes.length, matrix: world })
    doc.meshes.push({ primitives: [p] })
  }
  doc.buffers.push({ byteLength })
  const text = Buffer.from(JSON.stringify(doc))
  const jsonPadding = (4 - (text.length % 4)) % 4
  const binPadding = (4 - (byteLength % 4)) % 4
  const header = Buffer.alloc(20)
  header.write('glTF')
  header.writeUInt32LE(2, 4)
  header.writeUInt32LE(28 + text.length + jsonPadding + byteLength + binPadding, 8)
  header.writeUInt32LE(text.length + jsonPadding, 12)
  header.write('JSON', 16)
  const binHeader = Buffer.alloc(8)
  binHeader.writeUInt32LE(byteLength + binPadding)
  binHeader.writeUInt32LE(0x004e4942, 4)
  return Buffer.concat([
    header,
    text,
    Buffer.alloc(jsonPadding, 32),
    binHeader,
    ...chunks,
    Buffer.alloc(binPadding),
  ])
}

await mkdir(output, { recursive: true })
const rows = [],
  casters = []
let casterVertices = 0
// Use opaque structural surfaces for the first lighting pass.
const structural = /concrete|plaster|brick|roofing|awning_fabric|_fabric$|_beams$/i
for (const [id, group] of groups) {
  const filename = id + '.glb'
  await writeFile(join(output, filename), buildAsset(group.meshes))
  const material = input.materials[group.meshes[0].primitive.material]
  if (
    material &&
    (material.alphaMode ?? 'OPAQUE') === 'OPAQUE' &&
    !material.extensions?.KHR_materials_transmission &&
    structural.test(material.name ?? '')
  ) {
    casters.push(id)
    for (const { primitive } of group.meshes)
      casterVertices += input.accessors[primitive.indices ?? primitive.attributes.POSITION].count
  }
  for (const [i, world] of group.instances.entries())
    rows.push([id, filename, id + '-' + i, ...world].join(' '))
}
await writeFile(join(output, 'instances.txt'), rows.join('\n') + '\n')
await writeFile(join(output, 'casters.txt'), casters.join('\n') + '\n')
console.log(
  JSON.stringify({
    sourceMeshes: references.size,
    assets: groups.size,
    draws: rows.length,
    uniqueVertices,
    expandedVertices,
    shadowAssets: casters.length,
    casterVertices,
  }),
)
