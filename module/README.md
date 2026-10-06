# duke3d.wasm: the whole of Duke Nukem 3D as a TRACE module

JFDuke3D (with jfbuild, jfmact, jfaudiolib) from `../third_party`, compiled to WebAssembly and run by TERMinator's
sandbox (`gamesandbox`) on the player's own machine. **The game's own code is untouched** (`../patches` is empty so
far); everything replaced is the part that would talk to SDL, the sound hardware or the file system. Modelled on the
Wolfenstein 3D module (`../../Wolf3D/module`). Classic software renderer only (no Polymost/OpenGL), shareware v1.3D:
the game finds no known GRP in its scan, probes `DUKESW.BIN`, and runs as "Unregistered Shareware" (menu has
HOW TO ORDER, screen shows UNREGISTERED SHAREWARE).

## Build

```
make -C module                                   # needs wasi-sdk 34 in ~/tools
python3 module/mkpak.py data/DUKE3D.GRP third_party/soundfont/TimGM6mb.sf2 door/duke3d.pak   # prints its sha256
```

The first build after a patch changes can fail at the `rm -rf obj/jfduke3d` step while Dropbox holds the folder; the
Makefile retries once, and running `make` again always works. `make CFLAGS_EXTRA=-DDUKETRACE_STATS` logs the effects'
and music's levels and the frame rate every 5 s (how the levels in `src/audio_trace.c` were set).

## Headless

The pack goes in the assets folder named by its SHA-256 (`<sha>.bin`). Linux/WSL (Windows: the same with the `.exe`s
in `TERMinator-Windows/trace/bin`):

```
cd TERMinator-Windows/trace/bin-linux-x86_64
LD_LIBRARY_PATH=. ./gamesandbox_probe ./gamesandbox duke3d.wasm -seconds 28 -assets <dir> -data pak=<sha> \
    -keys 1@13000,28@14500,28@16000,28@17500 -shot game.bmp
```

The logos and title take about 12 s; Esc then opens the menu, and Enter three times is New Game, episode 1, skill.
`-data "pak=<sha> res=320x200|quit"` checks the door's quit (config goes up at once). `-data` holds at most 1024
bytes, so sending the player's files needs `-msgs <file>` (records of `u32 at_ms, u32 len, bytes`), which only a
probe built from today's `tools/gamesandbox_probe.c` has (`gcc -O2 tools/gamesandbox_probe.c -I include -lpthread
-lm`). The probe binaries ignore `TE_IN_QUIT` from `-recs` (the host stops on it itself), and the 2026-09-19 Windows
probe sends E0 keys as 256 + the code: the module accepts both.

Results (2026-10-06, WSL, 24 threads): 60 fps in E1L1 (capped; uncapped it draws ~470 fps at 640x400), sound written
in real time, whole run -25 dBFS RMS, peak -7.5. Screenshots in `shots/`.

## What's in here

| File | Replaces | What it does |
|---|---|---|
| `src/duketrace.c` | `main` | The TRACE entry points. Loads the pack, receives the player's files, starts `app_main` on its own thread (8 MB stack), turns the door's `quit` into "save the config and go", makes `exit()` wait for files to reach the BBS and close TERMinator's picture. |
| `src/tracelayer.c` | `jfbuild/src/sdlayer2.c` | Clock (`trace_time_ms`, Build's 120 Hz `totalclock`), keys (set-1 scancodes are already Build's key numbers; E0 keys get 0x80), US-layout characters for typing, video (one 8-bit mode, the door's size), `showframe` = palette to BGRA + `trace_present` at 4:3, capped at 60 fps. No mouse or joystick. |
| `src/audio_trace.c` | jfaudiolib's drivers | PCM: MultiVoc's mix buffers (as driver_sdl.c), 44.1 kHz S16 stereo. MIDI: jfaudiolib's own sequencer (midi.c) driving TinySoundFont with `timgm6mb.sf2` from the pack, a service call per MIDI tick. Both mixed on the game thread into `trace_audio_write`, the queue kept full. They answer drivers.c's "SDL" and "FluidSynth" slots (neither library is used). |
| `src/file_trace.c` | the file system | `open/read/lseek/close/fstat/stat/access` (descriptors of our own) and `fopen/fclose` (fmemopen / open_memstream) over memory: the pack's files, and the player's `duke3d.cfg` and `game0-9.sav`, which go to the BBS when written. Anything else (log, GRP scan cache, screenshots, demos) can't be written. |
| `include/duketrace_compat.h` | | Forced into every file: `exit` and the file calls as function-like macros. |
| `mkpak.py` | | Makes the pack. |
| `trace/trace_api.h` | | Copy of TERMinator's, so the source builds on its own. |

The resolution: 640x400 by default. Build gives 320x200 and 640x400 Mode 13h's tall pixels (engine.c `setgamemode`),
so at 4:3 it's the DOS picture with every 2D graphic scaled by exactly 2, and the ANSI/JXL door's `res=320x200` is
the same picture at half size. The module accepts any `res=` that is 320x200-shaped (8:5) or 4:3, 320-1600 wide,
width a multiple of 8; anything else keeps 640x400. The config's screen settings are ignored.

## The pack (`duke3d.pak`)

All little-endian, the Wolf3D pack's layout with its own magic: `"D3DPAK1\0"`, u32 count, count x
`{ char name[24]; u32 offset; u32 size }`, then the files. Two files, unchanged: `duke3d.grp`, `timgm6mb.sf2`
(17,005,643 bytes, sha256 `a6aa2dfed0d06668e9b4abf36e9f2b11826af2ae7e174bd104cf9e772abb2976` from the md5-checked
shareware GRP).

## Door messages

The same as Wolf3D's. Down (door to game):

- `file name=<n> off=<o> total=<t>\n<bytes>`: the player's files, before `pak=`, in order, pieces of any size up to
  the link's limit (3000 is what the probe test used). Names: `duke3d.cfg`, `game0.sav` ... `game9.sav` (lower
  case); anything else is ignored. Up to 4 MB each (a save is ~150 KB).
- `pak=<sha256> [res=<w>x<h>]`: starts the game.
- `quit`: time is up: the module writes `duke3d.cfg` (it goes up as `put`) and calls `trace_quit(0)`.

Up: `put name=<n> off=<o> total=<t>\n<bytes>`, pieces of 3000 bytes, sent whenever the game closes a file it wrote
(an unchanged file isn't re-sent). A newer copy starts again at `off=0`. On every exit (menu Quit, `quit`, error) the
module waits up to 5 s for pending pieces before `trace_quit`.

## Known gaps

- No mouse or joystick; keyboard only. Pause (0x59) has no set-1 code from TERMinator.
- Typed characters come from the keys on a US layout (TE_IN_TEXT is ignored), so other layouts type US characters.
- The in-game video-mode menu offers only the door's size. Screenshots, demo recording and the log do nothing.
- Music is quiet in E1L1 (-40 dBFS RMS) at the -9 dB TinySoundFont gain that matches the JPEG XL mode's
  pre-rendered music; the in-game music volume slider still works.
- Not run in a real TERMinator yet (probe only).
