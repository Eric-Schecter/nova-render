"""Fetch only Intel Sponza glTF geometry and license via HTTP Range (stdlib only)."""
import argparse
import binascii
import io
import pathlib
import struct
import urllib.request
import zipfile
import zlib

SOURCE = 'https://cdrdv2.intel.com/v1/dl/getContent/830833'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=pathlib.Path)
parser.add_argument('--list', action='store_true', help='Inspect entries without downloading geometry')
args = parser.parse_args()
with urllib.request.urlopen(urllib.request.Request(SOURCE, method='HEAD'), timeout=30) as response:
    url, size = response.geturl(), int(response.headers['Content-Length'])


def get(start, end):
    request = urllib.request.Request(url, headers={'Range': f'bytes={start}-{end}'})
    response = urllib.request.urlopen(request, timeout=180)
    if response.status != 206:
        response.close()
        raise RuntimeError('Server ignored Range; refusing to download the complete archive')
    return response


with get(max(0, size - 65536), size - 1) as response:
    tail = response.read()
end = tail.rfind(b'PK\x05\x06')
if end < 0:
    raise RuntimeError('ZIP directory not found')
record = struct.unpack_from('<4s4H2LH', tail, end)
length, offset = record[5:7]
if length == 0xffffffff or offset == 0xffffffff:
    raise RuntimeError('Unexpected ZIP64 archive; inspect the updated Intel package')
with get(offset, offset + length - 1) as response:
    central = response.read()


class RemoteDirectory(io.RawIOBase):
    position = 0

    def seek(self, count, whence=0):
        self.position = (size if whence == 2 else self.position if whence == 1 else 0) + count
        return self.position

    def tell(self):
        return self.position

    def read(self, count=-1):
        if count < 0:
            count = size - self.position
        start = self.position
        self.position += count
        if start >= offset and start + count <= offset + len(central):
            return central[start - offset:start - offset + count]
        if start >= size - len(tail):
            return tail[start - size + len(tail):start - size + len(tail) + count]
        with get(start, start + count - 1) as response:
            return response.read()


with zipfile.ZipFile(RemoteDirectory()) as archive:
    selected = [info for info in archive.infolist()
                if info.filename.endswith(('.gltf', '.bin', 'credits_license.txt'))]
args.directory.mkdir(parents=True, exist_ok=True)
for info in selected:
    print(info.filename, f'{info.file_size:,} bytes, {info.compress_size:,} compressed', flush=True)
    if args.list:
        continue
    if info.compress_size > 200 * 1024 * 1024:
        raise RuntimeError('Unexpected geometry size; inspect the updated package before downloading')
    with get(info.header_offset, info.header_offset + 30 + 131072 + info.compress_size) as response:
        header = struct.unpack('<4s5H3L2H', response.read(30))
        if header[0] != b'PK\x03\x04':
            raise RuntimeError('Invalid ZIP entry')
        response.read(header[-2] + header[-1])
        path = args.directory / pathlib.PurePosixPath(info.filename).name
        temporary = path.with_suffix(path.suffix + '.part')
        remaining, checksum, written = info.compress_size, 0, 0
        decoder = zlib.decompressobj(-15) if info.compress_type == 8 else None
        if info.compress_type not in (0, 8):
            raise RuntimeError('Unsupported compression')
        with temporary.open('wb') as output:
            while remaining:
                block = response.read(min(1024 * 1024, remaining))
                if not block:
                    raise RuntimeError('Truncated download')
                remaining -= len(block)
                data = decoder.decompress(block) if decoder else block
                output.write(data)
                checksum = binascii.crc32(data, checksum)
                written += len(data)
            data = decoder.flush() if decoder else b''
            output.write(data)
            checksum = binascii.crc32(data, checksum)
            written += len(data)
        if checksum != info.CRC or written != info.file_size:
            raise RuntimeError('ZIP checksum/size mismatch')
        temporary.replace(path)
