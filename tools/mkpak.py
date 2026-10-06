#!/usr/bin/env python3
"""
mkpak.py -- packs the Duke Nukem 3D shareware data and the General MIDI soundfont into duke3d.pak, the one asset the
door sends. (The Wolfenstein 3D door's mkpak.py, for Duke.)

    python3 tools/mkpak.py data/DUKE3D.GRP third_party/soundfont/TimGM6mb.sf2 door/duke3d.pak

Format (read by module/src/duketrace.c), all little-endian:
    "D3DPAK1\\0", u32 count, count x { char name[24]; u32 offset; u32 size }, then the files one after another.

The two files go in unchanged (the shareware terms ask that DUKE3D.GRP is passed on unmodified). Names are lower case
(duke3d.grp, timgm6mb.sf2) and sorted, so the same files always make the same pack, and so the same hash: players
who already have it aren't sent it again.
"""
import hashlib
import os
import struct
import sys

SHAREWARE_MD5 = 'c03558e3a78d1c5356dc69b6134c5b55'    # DUKE3D.GRP, shareware v1.3D


def main():
    if len(sys.argv) != 4:
        sys.exit('usage: mkpak.py <DUKE3D.GRP> <TimGM6mb.sf2> <out.pak>')
    grp, sf2, out = sys.argv[1:]
    files = {}
    for path in (grp, sf2):
        with open(path, 'rb') as f:
            files[os.path.basename(path).lower()] = f.read()
    if hashlib.md5(files[os.path.basename(grp).lower()]).hexdigest() != SHAREWARE_MD5:
        # Only the shareware episode may be handed out: the registered game's GRP must never go in
        sys.exit(f'{grp}: not the shareware DUKE3D.GRP v1.3D (md5 {SHAREWARE_MD5}); refusing to pack it')
    if 'duke3d.grp' not in files:
        sys.exit(f'{grp}: must be named DUKE3D.GRP')
    names = sorted(files)
    offset = 12 + 32 * len(names)
    table = b''
    for n in names:
        table += struct.pack('<24sII', n.encode(), offset, len(files[n]))
        offset += len(files[n])
    with open(out, 'wb') as f:
        f.write(b'D3DPAK1\0' + struct.pack('<I', len(names)) + table + b''.join(files[n] for n in names))
    print(f'{out}: {len(names)} files, {offset} bytes')


if __name__ == '__main__':
    main()
