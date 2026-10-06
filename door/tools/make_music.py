#!/usr/bin/env python3
"""
make_music.py -- turns midirender's WAV files into the pieces the JPEG XL graphics mode plays (door/music/).
(The DOOM door's tools/make_music.py, for Duke.)

Each song becomes 5-second Ogg Vorbis files, mono at 22050 Hz, quality 4 (about 5.5 KB a second; DOOM measured
quality 0 at 17-21 dB signal to noise, audibly gritty, 4 at 23-29). The door uploads a piece to the caller's cache
only when it's about to be played, so a song starts within a moment and a slow connection is never tied up sending a
whole one; the caller's terminal plays the pieces back to back on one channel, seamlessly (Vorbis keeps each piece's
exact length).

    ./midirender ../data/DUKE3D.GRP ../third_party/soundfont/TimGM6mb.sf2 /tmp/dukemusic
    python3 tools/make_music.py /tmp/dukemusic music

Writes music/<SONG>_<nn>.ogg and music/index.txt, one line per song:
    <SONG> <sha256 of the MIDI file> <pieces> <length of the last piece in ms> <loop start in ms> <loop piece>
DOOM's index, plus where the song loops: Duke's songs loop back to a point after their opening (EMIDI, see
midirender.c), and the terminal can only start a piece from its beginning, so the song is cut there. The opening
(0 to the loop start) is cut into 5-second pieces, its last one shorter, ending exactly at the loop start; the rest
from the loop start on. <loop piece> is the first piece after the opening, the one the song goes back to (0 when the
whole song repeats); every piece is 5 s but the opening's last and the song's last.
Needs ffmpeg with libvorbis. Made from the game's data: ships with the door, not in the public source.
"""

import os
import subprocess
import sys

RATE = 22050
PIECE = 5 * RATE          # samples a piece
QUALITY = '4'


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for name in os.listdir(dst):
        if name.endswith('.ogg') or name == 'index.txt':
            os.remove(os.path.join(dst, name))

    songs = [line.split() for line in open(os.path.join(src, 'tracks.txt')) if line.strip()]
    index = []
    for song, midi_hash, loop_ms in songs:
        # the whole song, mono at 22050 Hz, as raw samples: cut from this so the pieces join exactly
        pcm = subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', os.path.join(src, song + '.wav'),
                              '-ac', '1', '-ar', str(RATE), '-f', 's16le', '-'],
                             check=True, capture_output=True).stdout
        samples = len(pcm) // 2
        loop = min(int(loop_ms) * RATE // 1000, samples - 1)
        # where each piece starts: the opening in 5 s pieces up to the loop start, then on from there
        starts = list(range(0, loop, PIECE)) + list(range(loop, samples, PIECE))
        loop_piece = starts.index(loop)
        pieces = len(starts)
        for k in range(pieces):
            end = starts[k + 1] if k + 1 < pieces else samples
            chunk = pcm[starts[k] * 2:end * 2]
            subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-f', 's16le', '-ar', str(RATE), '-ac', '1',
                            '-i', '-', '-c:a', 'libvorbis', '-q:a', QUALITY,
                            # bitexact: the same stream serial every time, so re-running this doesn't change a piece
                            # whose sound hasn't (its md5 is how the door knows a caller already has it)
                            '-fflags', '+bitexact', '-flags:a', '+bitexact',
                            os.path.join(dst, f'{song}_{k:02d}.ogg')], input=chunk, check=True)
        last_ms = (samples - starts[-1]) * 1000 // RATE
        # the loop start as the pieces have it (whole samples), so the door's timing adds up exactly
        index.append(f'{song} {midi_hash} {pieces} {last_ms} {loop * 1000 // RATE} {loop_piece}\n')
        print(f'{song}: {pieces} pieces')
    with open(os.path.join(dst, 'index.txt'), 'w') as f:
        f.writelines(index)


if __name__ == '__main__':
    main()
