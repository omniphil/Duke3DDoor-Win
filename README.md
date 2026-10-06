# DUKE NUKEM 3D for TERMinator: a BBS door for Windows

3D Realms' Duke Nukem 3D (1996), shareware episode 1 "L.A. Meltdown" (v1.3D), playable from a BBS running
**Mystic BBS for Windows**. This is the source of `duke3ddoor.exe`; the same source also builds the Linux door
(https://github.com/omniphil/Duke3DDoor). It works like the DOOM door (https://github.com/omniphil/DOOMDoor) and the
Wolfenstein 3D door (https://github.com/omniphil/Wolf3DDoor):

- **TRACE** (TERMinator 1.1.2+): the whole game runs on the caller's PC inside TERMinator's sandbox at 640x400, with
  General MIDI music and digitised sound. The BBS sends it once; after that almost nothing crosses the wire.
- **JPEG XL graphics** (terminals that speak the CTerm APC picture and sound commands): the door runs the game on the
  BBS and sends its 320x200 picture as JPEG XL, up to 30 frames a second at a quality that follows the link, with its
  music and effects played from the caller's own cache. Needs libjxl's `jxl.dll` beside the door.
- **ANSI 24-bit / 256 / 16** (every other terminal): the door runs the same game on the BBS and sends it as ANSI
  half-blocks, with the menus redrawn as text and a text status line. It is silent.

The start page lets each caller pick, and remembers the choice.

**This repository exists so that anyone who plays the door can have the source of the game they were sent**, which is
what the GNU GPL asks for (see `door/LICENSE.md`). It is the address the Windows door itself gives out.

| Folder | What |
|---|---|
| `third_party/jfduke3d` | JFDuke3D (github.com/jonof/jfduke3d 55c5f95, with jfbuild 40a98be, jfaudiolib 68be97f, jfmact 731c0cc), unmodified. 3D Realms' code under the GPL (`GPL.TXT`) |
| `third_party/TinySoundFont` | TinySoundFont (github.com/schellingb/TinySoundFont 853a0a1, MIT), the MIDI synthesiser |
| `third_party/soundfont` | `TimGM6mb.sf2`, a General MIDI sound font by Tim Brechbill (GPL-2) |
| `module/` | `duke3d.wasm`, the game as a TRACE module (see `module/README.md`) |
| `door/` | `duke3ddoor.exe` / `duke3ddoor` (see `door/INSTALL.md`) |
| `door/patches/` | the door's one change to its own copy of JFDuke3D (menu and status text for the ANSI mode) |
| `tools/get_shareware.sh` | fetches the shareware `DUKE3D.GRP` into `data/`. **Not in the repository**: it is 3D Realms', given away as shareware (its LICENSE.TXT lets free BBSs and BBSs of 250 nodes or fewer offer it) |
| `tools/mkpak.py` | packs the GRP and the sound font into `door/duke3d.pak`, the one asset the door sends |
| `tools/winlaunch.c`, `tools/test_windoor.py` | test the Windows door without a BBS: `winlaunch` plays Mystic for Windows (a telnet socket handed over in DOOR32.SYS) |

## Build

On Linux or WSL, with mingw-w64 (`sudo apt install mingw-w64`) for the Windows door and
[wasi-sdk](https://github.com/WebAssembly/wasi-sdk) 34 in `~/tools` for the TRACE module:

```
sh tools/get_shareware.sh                         # the shareware data, into data/
make -C module && cp module/duke3d.wasm door/     # the TRACE module
python3 tools/mkpak.py data/DUKE3D.GRP third_party/soundfont/TimGM6mb.sf2 door/duke3d.pak
make -C door win64                                # door/duke3ddoor.exe (64-bit; make win32 for 32-bit, untested)
make -C door music sound                          # the JPEG XL mode's sound (needs ffmpeg with libvorbis)
```

`door/music/` and `door/sound/` are made from 3D Realms' data, so they aren't in the repository. Without them the
JPEG XL mode plays silently (TRACE and ANSI don't use them). Installing in Mystic for Windows: `door/INSTALL.md`,
"Windows". Plain `make -C door` builds the Linux door from the same source.
