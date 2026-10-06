#!/usr/bin/env python3
"""
make_sfx.py -- takes Duke Nukem 3D's sound effects out of DUKE3D.GRP for the JPEG XL graphics mode (door/sound/).

    python3 tools/make_sfx.py ../data/DUKE3D.GRP sound

That mode plays the game's sounds on the caller's own terminal from files in its cache (uploaded once, checked by md5
on each call, like the Wolfenstein door's digitised sounds). The terminal plays WAV files, 8-bit, so each effect goes
as one: Duke's are Creative Voice files (.VOC), 8-bit unsigned mono at 6-22 kHz, which become WAV unchanged
(same samples, same rate; the one 16-bit VOC, SHOTGUN7, keeps its top 8 bits). A WAV in the GRP (none in the shareware) is copied if it's already 8-bit mono, else
converted with ffmpeg.

Writes sound/<name>.wav (lower case, as the game's CON files name them) and sound/index.txt, one line per effect:
    sfx <NAME as in the GRP> <file> <rate> <length in ms> <loop start> <loop end>
(loop start/end in samples, from a VOC's repeat blocks, -1 when it has none; the game loops some effects -- engines,
water -- from its own sound table, not from the file.)
Made from the game's data: ships with the door, not in the public source.
"""

import os
import struct
import subprocess
import sys
import wave


def grp_files(path):
    data = open(path, 'rb').read()
    if data[:12] != b'KenSilverman':
        sys.exit(f'{path}: not a GRP file')
    count = struct.unpack_from('<I', data, 12)[0]
    off = 16 + 16 * count
    for i in range(count):
        name = data[16 + i * 16:28 + i * 16].split(b'\0')[0].decode('ascii', 'replace')
        size = struct.unpack_from('<I', data, 28 + i * 16)[0]
        yield name, data[off:off + size]
        off += size


def voc_to_pcm(voc):
    """A VOC's samples as 8-bit unsigned mono, its rate, and its loop (start, end) in samples or (-1, -1)."""
    if not voc.startswith(b'Creative Voice File\x1a'):
        raise ValueError('not a VOC file')
    pos = struct.unpack_from('<H', voc, 20)[0]
    pcm, rate, loop = bytearray(), None, [-1, -1]
    ext_rate = None
    while pos < len(voc):
        kind = voc[pos]
        if kind == 0:
            break
        size = voc[pos + 1] | voc[pos + 2] << 8 | voc[pos + 3] << 16
        body = voc[pos + 4:pos + 4 + size]
        pos += 4 + size
        if kind == 1:                       # sound data: rate byte, codec, samples
            codec = body[1]
            if codec != 0:
                raise ValueError(f'VOC codec {codec} (only 8-bit PCM is handled)')
            r = ext_rate or round(1000000 / (256 - body[0]))
            ext_rate = None
            if rate is None:
                rate = r
            pcm += body[2:]
        elif kind == 2:                     # more samples
            pcm += body
        elif kind == 3:                     # silence: length - 1, rate byte
            n = struct.unpack_from('<H', body, 0)[0] + 1
            if rate is None:
                rate = round(1000000 / (256 - body[2]))
            pcm += b'\x80' * n
        elif kind == 6:                     # repeat start
            loop[0] = len(pcm)
        elif kind == 7:                     # repeat end
            loop[1] = len(pcm)
        elif kind == 8:                     # extended: the next block's real rate
            tc, _, mode = struct.unpack_from('<HBB', body, 0)
            channels = mode + 1
            ext_rate = round(256000000 / (channels * (65536 - tc)))
            if channels != 1:
                raise ValueError('stereo VOC')
        elif kind == 9:                     # new-style sound data: rate, bits, channels, codec
            r, bits, channels, codec = struct.unpack_from('<IBBH', body, 0)
            if channels != 1 or (bits, codec) not in ((8, 0), (16, 4)):
                raise ValueError(f'VOC block 9 with {bits} bits, {channels} channels, codec {codec}')
            if rate is None:
                rate = r
            if bits == 8:
                pcm += body[12:]
            else:                           # 16-bit signed (SHOTGUN7.VOC): to 8-bit unsigned, the top byte
                samples = body[12:12 + (len(body) - 12) // 2 * 2]
                pcm += bytes(((v >> 8) + 128) & 0xFF for (v,) in struct.iter_unpack('<h', samples))
    if rate is None:
        raise ValueError('no sound in it')
    return bytes(pcm), rate, tuple(loop)


def write_wav8(path, pcm, rate):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(rate)
        w.writeframes(pcm)


def main():
    grp, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for name in os.listdir(dst):
        if name.endswith('.wav') or name == 'index.txt':
            os.remove(os.path.join(dst, name))

    index, total, skipped = [], 0, []
    for name, data in grp_files(grp):
        ext = name.rsplit('.', 1)[-1].upper()
        if ext not in ('VOC', 'WAV'):
            continue
        out = name.rsplit('.', 1)[0].lower() + '.wav'
        path = os.path.join(dst, out)
        loop = (-1, -1)
        try:
            if ext == 'VOC':
                pcm, rate, loop = voc_to_pcm(data)
                write_wav8(path, pcm, rate)
                samples = len(pcm)
            else:
                tmp = path + '.in'
                open(tmp, 'wb').write(data)
                subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', tmp, '-ac', '1', '-c:a', 'pcm_u8',
                                '-fflags', '+bitexact', '-flags:a', '+bitexact', path], check=True)
                os.remove(tmp)
                with wave.open(path) as w:
                    rate, samples = w.getframerate(), w.getnframes()
        except (ValueError, struct.error, subprocess.CalledProcessError) as e:
            skipped.append(f'{name} ({e})')
            continue
        total += os.path.getsize(path)
        index.append(f'sfx {name} {out} {rate} {samples * 1000 // rate} {loop[0]} {loop[1]}\n')
    with open(os.path.join(dst, 'index.txt'), 'w') as f:
        f.writelines(index)
    print(f'{dst}: {len(index)} effects, {total // 1024} KB')
    for s in skipped:
        print(f'  skipped {s}')


if __name__ == '__main__':
    main()
