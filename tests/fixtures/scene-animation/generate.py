"""Add two rigid-node TRS clips to Nova's procedural box; no external assets."""
import json
import struct
from pathlib import Path
root = Path(__file__).resolve().parents[3]
source = (root / 'assets/SceneSample/Red.glb').read_bytes()
size = struct.unpack_from('<I', source, 12)[0]
doc = json.loads(source[20:20 + size])
bin_size = struct.unpack_from('<I', source, 20 + size)[0]
binary = bytearray(source[28 + size:28 + size + bin_size])
parent = len(doc['nodes'])
doc['nodes'].append({'children': doc['scenes'][doc.get('scene', 0)]['nodes'], 'translation': [0, 0, 0]})
doc['scenes'][doc.get('scene', 0)]['nodes'] = [parent]
def accessor(values, kind, components):
    binary.extend(b'\0' * (-len(binary) % 4))
    offset = len(binary)
    binary.extend(struct.pack('<' + 'f' * len(values), *values))
    view = len(doc['bufferViews'])
    doc['bufferViews'].append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(values) * 4})
    index = len(doc['accessors'])
    doc['accessors'].append({'bufferView': view, 'componentType': 5126, 'count': len(values) // components, 'type': kind})
    return index
times = accessor([0, 1], 'SCALAR', 1)
doc['animations'] = []
for name, target in [('Slide', [1, 0, 0]), ('Lift', [0, 1, 0])]:
    output = accessor([0, 0, 0] + target, 'VEC3', 3)
    doc['animations'].append({'name': name, 'samplers': [{'input': times, 'output': output, 'interpolation': 'LINEAR'}], 'channels': [{'sampler': 0, 'target': {'node': parent, 'path': 'translation'}}]})
doc['buffers'][0]['byteLength'] = len(binary)
encoded = json.dumps(doc, separators=(',', ':')).encode()
encoded += b' ' * (-len(encoded) % 4)
binary.extend(b'\0' * (-len(binary) % 4))
output = struct.pack('<III', 0x46546c67, 2, 28 + len(encoded) + len(binary)) + struct.pack('<II', len(encoded), 0x4e4f534a) + encoded + struct.pack('<II', len(binary), 0x004e4942) + binary
(root / 'tests/fixtures/scene-animation/Node.glb').write_bytes(output)
