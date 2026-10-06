#!/usr/bin/env python3
"""
mkpak.py -- packs the Duke Nukem 3D shareware data (DUKE3D.GRP, v1.3D) and the General MIDI sound font
(TimGM6mb.sf2) into duke3d.pak, the one asset the door sends for the TRACE module.

    python3 module/mkpak.py data/DUKE3D.GRP third_party/soundfont/TimGM6mb.sf2 door/duke3d.pak

Format (read by module/src/duketrace.c), all little-endian, the Wolfenstein 3D pack's layout with its own magic:
    "D3DPAK1\\0", u32 count, count x { char name[24]; u32 offset; u32 size }, then the files one after another.

The files go in unchanged (as the shareware terms ask) under the names the module looks for: duke3d.grp and
timgm6mb.sf2, lower case, in that order, so the same inputs always make the same pack, and so the same SHA-256:
players who already have it aren't sent it again. Prints the SHA-256 the door names it by.
"""
import hashlib
import struct
import sys

def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    grp, sf2, out = sys.argv[1:]
    files = [('duke3d.grp', grp), ('timgm6mb.sf2', sf2)]
    blobs = []
    for _, path in files:
        with open(path, 'rb') as f:
            blobs.append(f.read())
    offset = 12 + 32 * len(files)
    table = b''
    for (name, _), blob in zip(files, blobs):
        table += struct.pack('<24sII', name.encode(), offset, len(blob))
        offset += len(blob)
    pak = b'D3DPAK1\0' + struct.pack('<I', len(files)) + table + b''.join(blobs)
    with open(out, 'wb') as f:
        f.write(pak)
    print(f'{out}: {len(files)} files, {len(pak)} bytes, sha256 {hashlib.sha256(pak).hexdigest()}')

if __name__ == '__main__':
    main()
