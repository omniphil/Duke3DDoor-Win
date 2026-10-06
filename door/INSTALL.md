# Installing the DUKE NUKEM 3D door

The start page offers each caller (remembered in their `saves/<player>/display.cfg`):

| Choice | What it is | State |
|---|---|---|
| 1. TRACE graphics (+ Sound) | the door sends the game and TERMinator runs it on the caller's machine, with MIDI music | **works** |
| 2. JPEG XL graphics (320x200 + Sound) | the game runs here, its picture goes as JPEG XL and its sound plays from the terminal's cache | works (headless tests) |
| 3-5. ANSI 24-bit / 256 / 16 | the game runs here, sent as half-blocks | works (headless tests) |

The menu shows what was detected (the Wolfenstein and DOOM doors' detection: JPEG XL needs CTerm 1.329+, older ones
see UPDATE TERMINAL; before 1.332 frames go pre-scaled to 640x400). For 2-5 the door runs the very same game itself:
JFDuke3D and `../module/src` are compiled into `duke3ddoor` (`ansi_host.c` answers the module's `trace_*` calls). The
door runs on
**Linux** (Mystic for Linux, or any BBS that runs DOOR32 doors) and **Windows** (Mystic for Windows): same source, `platform_posix.c` /
`platform_win32.c`.

## What the door needs

| File | Where it comes from | Size |
|---|---|---|
| `duke3ddoor` (Linux) or `duke3ddoor.exe` (Windows) | `make` / `make win64` here (the game is compiled in) | 1.1 MB / 1.7 MB |
| `duke3d.wasm` | `make` in `../module` (needs wasi-sdk); `make install` copies it in | ~850 KB |
| `duke3d.pak` | `DUKE3D.GRP` + `TimGM6mb.sf2`, packed by `make install` or `../tools/mkpak.py` | 17 MB |
| `music/`, `sound/` | for the JPEG XL mode: `make music`, `make sound` | 13 MB, 3 MB |
| `native/` | the game's sources, put here by `make bundle`, so the folder builds on the BBS box on its own | 5 MB |
| `patches/` | the door's own change to its copy of JFDuke3D (below) | |
| libjxl | Linux: the system's `libjxl.so` (0.7+); Windows: `jxl.dll` + its DLLs beside `duke3ddoor.exe`. Without it the JPEG XL mode isn't offered | |
| `saves/` | made by the door: one folder per player | |

The pack only goes to a caller once (TERMinator caches it by SHA-256); the first time is 17 MB, about 30 s at
500 KB/s.

## Linux (Mystic for Linux)

1. On the development machine: `sh ../tools/get_shareware.sh` (once), `make -C ../module`, then here
   `make install` (builds `duke3ddoor`, copies `duke3d.wasm`, packs `duke3d.pak`).
2. Copy the door folder to the BBS as `~/mystic/doors/duke3d/` (`duke3ddoor`, `duke3d.wasm`, `duke3d.pak`,
   `LICENSE.md`; `music/` and `sound/` when the JPEG XL mode comes). Rebuild there with `make` if the box's libc
   differs.
3. Menu command: `(D3) Exec DOOR32 program`, data
   `./doors/duke3d/duke3ddoor /path/to/mystic/temp%3`
   (the folder Mystic writes `door32.sys` in for node `%3`). Without a drop file every caller shares
   `saves/player/`.

Mystic for Linux runs the door on a pseudo-terminal and does the telnet itself; the door reads stdin and writes
stdout, in raw mode, XON/XOFF off (Ctrl-Q reaches it).

## Windows (Mystic for Windows)

1. Build on Linux/WSL with mingw-w64 (`sudo apt install mingw-w64`):
   `make win64` makes `duke3ddoor.exe`, **the Windows door** (64-bit, for any current Windows; this is the one that is
   tested). Linked statically: it needs only Windows' own DLLs (kernel32, msvcrt, ws2_32, shell32).
   Untested extra: `make win32` makes `win32/duke3ddoor.exe` for 32-bit Windows. It compiles and links but has never
   been run; its JPEG XL mode would need a 32-bit `jxl.dll`.
2. Copy `duke3ddoor.exe`, `duke3d.wasm`, `duke3d.pak`, `music\`, `sound\` and `LICENSE.md` into e.g.
   `C:\mystic\doors\duke3d\`.
   The door keeps `saves\` beside the .exe, so that folder must be writable by Mystic's user.
3. Menu command: `(D3) Exec DOOR32 program`, data
   `C:\mystic\doors\duke3d\duke3ddoor.exe C:\mystic\temp%3`
   (Mystic's temp folder for the node, where it writes `DOOR32.SYS`; either case of the name is found).

Mystic for Windows hands the door the caller's **telnet socket** (DOOR32.SYS line 1 = 2, line 2 = the socket handle,
inherited). The door calls `WSAStartup` and talks on that socket with `send`/`recv`/`select`, doing the telnet layer
itself (`telnet.c`): 0xFF doubled going out; IAC commands, subnegotiations and the NUL after CR taken out coming in.
It never closes the socket (Mystic carries on with it). With no drop file, or comm type 0, it uses the console, for
trying it locally. A crash never leaves an error box waiting on the BBS machine (`SetErrorMode`), and an unhandled
exception puts the caller's terminal back to plain text before the door ends.

**JPEG XL mode:** the door loads `jxl.dll` (or `libjxl.dll`) **only from its own folder**, so put libjxl's
DLLs there: `jxl.dll`, `jxl_cms.dll`, `jxl_threads.dll` if present, `brotlicommon.dll`, `brotlienc.dll`,
`hwy.dll`, `lcms2.dll` (whatever the libjxl build depends on; a static `jxl.dll` needs none). Without them that mode
is shown as unavailable and everything else works.

## Checking it works (on the development machine, never on the BBS box)

```
python3 test_ansi.py 3 /tmp/out             # ANSI headless: screenshots of title, menus, E1L1 (4 = 256, 5 = 16)
python3 tools/fake_cterm.py /tmp/jxl --kbps 500 --ping 80   # JPEG XL headless: screenshots, apc.log, game.log
python3 ../tools/test_windoor.py [--bin]    # the Windows door's TRACE mode, run through WSL (tools/winlaunch.c)
```

`fake_cterm.py` plays a CTerm terminal (1.332; `--oldcterm` 1.331, `--nokeys` no key reports, `--keepcache` a second
call, `--menuwait N` to hear the title song loop). `DUKEDOOR_LOG=<file>` writes the game's console and, in the JPEG XL
mode, its numbers every 5 s; otherwise they go to `saves/<player>/jxl.log`. `saves/<player>/linktest.cfg`
(`kbps=500`, `ping=80`) plays that player's JPEG XL games through a modelled link.

```
python3 test_door.py            # a fake TERMinator: game and data arrive intact, config first, saves kept
python3 test_door.py --bin      # the same with binary frames (bin=1)
python3 test_door.py --plain    # no TRACE: it is marked NOT FOUND and the caller can go back
make test-telnet                # the Windows door's telnet layer, tested on Linux
rm -rf saves                    # the tests leave this behind
```

The Windows build's TRACE mode passes `../tools/test_windoor.py` and its ANSI mode was smoke-tested through
`../tools/winlaunch.exe` under WSL (menus, E1L1, Ctrl-Q saving the config); JPEG XL on Windows hasn't been run (no
`jxl.dll` here) and nothing on a real Mystic: try it first on a Windows Mystic node
with a test account.

## The ANSI mode

80x24: the picture on rows 1-22 (half-blocks, ANSI pacing by `ESC[6n` answers as in Wolfenstein), the whole screen as
the view (the game's status bar is switched off: `duke_force_screen_size`), a text status line on row 23 (level,
health, armour, weapon and ammo, inventory item, keycards), the keys on row 24. Duke's fonts are pictures and can't
be read at 80 columns, so the door's patch (`patches/door-text-hooks.patch`, door only: the TRACE module doesn't have
it) reports every text the game draws and where its menu cursor is; the door draws them as text in place, the chosen
item in white on red. Silent: the effects are mixed and thrown away (the game's timing needs it), the music isn't
synthesised at all.

Keys (no key-up from a terminal: Wolfenstein's timed holds, tuned to Duke's 30 tics a second): arrows or W/S move,
`,` `.` strafe, F fires, Space or E opens, A jumps, Z crouches, R toggles the game's own Auto Run, Q or ` kicks,
1-0 weapons, Enter / `[` `]` the inventory, Tab the map, PgUp/PgDn look, Esc the menu, Ctrl-Q back to the BBS (the
game saves its settings first), `\` frames a second. Its settings are its own (`ansi-duke3d.cfg`).

## The JPEG XL mode

The DOOM / Wolfenstein design (https://github.com/omniphil/DOOMDoor and https://github.com/omniphil/Wolf3DDoor): 320x200 frames as JPEG XL, tile diffs, CPR-paced
window, quality from the link, at most 30 a second (Duke draws 60; a frame waits for the window within its turn
rather than giving it up). Keys: with key reports, Duke's own (walk, Shift runs, F fires as well as Ctrl); without,
the ANSI mode's. Ctrl-Q quits. Crash guard (platform.h: signals on Linux, the exception filter on Windows) stops the
sound and puts the terminal back.

Sound (`pix_hooks.c`, linked with `--wrap`): jfaudiolib's `FX_PlayAuto3D`/`FX_PlayLoopedAuto`/`FX_Pan3D`/
`FX_StopSound`/`FX_StopAllSounds` become effect events by sound number (its GRP file from the game's `sounds[]`), with
MultiVoc's own left/right levels; `MUSIC_PlaySong` (by the MIDI's sha256), stop, pause, continue and volume become
music events. Effects (`sound/`, 3 MB) go up once, checked by md5, and are loaded into the terminal's slots when first
played; looping ones queue with `L`. Pitch changes aren't carried over. Music pieces (`music/`) go up as needed;
each song is cut at its loop start, so it loops back exactly where the game's player does. Levels match the TRACE
module (effects x0.35, music -9 dB and the game's music volume as a cubed channel volume). Measured with
`fake_cterm.py` on a modelled 500 KB/s, 80 ms link: 28-29 fps while moving, distance 1.8-3; pre-scaled (CTerm
1.331): 27-29 fps at distance ~7.5.

On Windows the game's files that go through stdio use temporary files in `%TMP%` (deleted on close), and its stdout
and stderr go to NUL (on Linux, `/dev/null`: the door keeps its own copy of the connection), so the game's console
messages never reach the caller.

## Saved games

Kept per player in `saves/<handle>-<user number>/`: `duke3d.cfg` (settings and keys) and `game0.sav`-`game9.sav`.
The door sends every one to the game when it starts (3000-byte pieces: `file name=.. off=.. total=..`), then
`pak=<sha256>`; the game sends each back when it writes it (`put name=.. off=.. total=..`), and the door asks it to
`quit` (so it saves) if the caller's time runs out. A file arriving is written beside the old one and moved over it
when whole; the one it replaces is kept as `.bak`. Any other name the game sends is ignored. ANSI mode keeps its own
`ansi-duke3d.cfg` (whole-screen view); the JPEG XL mode uses TRACE's `duke3d.cfg`. Saved games are shared by all.

## Music and sound for the JPEG XL mode

Made from the game's data, so they ship with the door but aren't in the public source. Need `../data/DUKE3D.GRP`
(or `GRP=<path>`) and, for music, ffmpeg with libvorbis (`sudo apt install ffmpeg`).

```
make music      # tools/midirender.c renders each song with TinySoundFont + TimGM6mb (/tmp/dukemusic),
                # tools/make_music.py cuts it into 5 s Ogg Vorbis pieces (q4, 22050 Hz mono, bitexact)
make sound      # tools/make_sfx.py: the 181 VOC effects as 8-bit mono WAV at their own rates
```

`music/index.txt`: `<SONG> <sha256 of the MIDI> <pieces> <last piece ms> <loop start ms> <loop piece>` (DOOM's index
plus the loop: Duke's EMIDI songs loop back to a point after their opening, e.g. STALKER from 17.7 s, so the opening
is cut to end exactly there and `<loop piece>` is the piece the song goes back to). The songs are
rendered as the game plays them on a General MIDI device: EMIDI tracks for other cards left out, EMIDI program and
volume changes used, ending at the song's loop end. Gain -9 dB (0 dB clips four of the seven songs).

`sound/index.txt`: `sfx <GRP name> <file> <rate> <ms> <loop start> <loop end>`.
