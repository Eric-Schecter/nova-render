#!/usr/bin/env python3
"""Package pinned opaque reference meshes; no renderer/importer behavior is changed."""
import argparse
import base64
import hashlib
import json
import posixpath
import struct
import zipfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('pillar', 'bunny', 'ground', 'reflections_test', 'global_illumination_test')


def prepare(archive, output, profile):
    manifest = json.loads((ROOT / 'tests/fixtures/hybrid-reference/manifest.json').read_text())
    digest = hashlib.sha256()
    with archive.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    if digest.hexdigest() != manifest['archive']['sha256']:
        raise ValueError('Reference archive SHA-256 differs from the pinned manifest')
    output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as source:
        for name in NAMES:
            document = json.loads(source.read('meshes/' + name + '.gltf'))
            if len(document['buffers']) != 1 or document.get('skins') or document.get('animations'):
                raise ValueError('Expected a single-buffer static reference mesh')
            uri = document['buffers'][0].pop('uri')
            binary = bytearray(source.read(posixpath.normpath(posixpath.join('meshes', uri))))
            for image in document.get('images', []):
                uri = image.pop('uri', None)
                if uri is None:
                    continue
                if uri.startswith('data:'):
                    header, payload = uri.split(',', 1)
                    if ';base64' not in header:
                        raise ValueError('Expected base64 image data')
                    data = base64.b64decode(payload, validate=True)
                else:
                    data = source.read(posixpath.normpath(posixpath.join('meshes', uri.replace('\\', '/'))))
                if data.startswith(b'\x89PNG\r\n\x1a\n'):
                    mime = 'image/png'
                elif data.startswith(b'\xff\xd8'):
                    mime = 'image/jpeg'
                else:
                    raise ValueError('Reference image needs explicit PNG/JPEG conversion')
                binary.extend(b'\0' * (-len(binary) % 4))
                view = {'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(data)}
                binary.extend(data)
                image['bufferView'] = len(document.setdefault('bufferViews', []))
                image['mimeType'] = mime
                document['bufferViews'].append(view)
            if profile == 'reference-import':
                # Pinned dwSampleFramework Mesh::load reads aiMesh vertices and
                # never traverses node transforms. Reproduce that convention
                # here, while leaving Nova's conforming glTF importer intact.
                document['nodes'] = [{'mesh': i} for i in range(len(document['meshes']))]
                document['scenes'] = [{'nodes': list(range(len(document['nodes'])))}]
                document['scene'] = 0
            document['buffers'][0]['byteLength'] = len(binary)
            encoded = json.dumps(document, separators=(',', ':'), allow_nan=False).encode('utf8')
            encoded += b' ' * (-len(encoded) % 4)
            binary.extend(b'\0' * (-len(binary) % 4))
            glb = (struct.pack('<III', 0x46546C67, 2, 28 + len(encoded) + len(binary))
                   + struct.pack('<II', len(encoded), 0x4E4F534A) + encoded
                   + struct.pack('<II', len(binary), 0x004E4942) + binary)
            (output / (name + '.glb')).write_bytes(glb)
            print(name, 'profile=' + profile, len(glb), 'bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=ROOT / 'build/reference/hybrid-rendering/HybridRendering_1.1.0.zip')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/reference/hybrid-rendering/models')
    parser.add_argument('--profile', choices=('gltf', 'reference-import'), default='reference-import')
    parser.add_argument('--download', action='store_true', help='Fetch the pinned archive when absent')
    args = parser.parse_args()
    if args.download and not args.archive.exists():
        manifest = json.loads((ROOT / 'tests/fixtures/hybrid-reference/manifest.json').read_text())
        args.archive.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.archive.with_suffix('.download')
        try:
            with urllib.request.urlopen(manifest['archive']['url'], timeout=120) as response, temporary.open('wb') as target:
                while block := response.read(1024 * 1024):
                    target.write(block)
            if temporary.stat().st_size != manifest['archive']['bytes'] or hashlib.sha256(temporary.read_bytes()).hexdigest() != manifest['archive']['sha256']:
                raise ValueError('Downloaded reference archive does not match pinned bytes/SHA-256')
            temporary.replace(args.archive)
        finally:
            temporary.unlink(missing_ok=True)
    prepare(args.archive, args.output, args.profile)
