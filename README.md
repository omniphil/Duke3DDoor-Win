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

Two ways to build the same thing; pick whichever suits you. Both end with these files in `door/`, which go to the BBS
(see `door/INSTALL.md`, "Windows"): `duke3ddoor.exe`, `duke3d.wasm`, `duke3d.pak`, `music\`, `sound\`, `LICENSE.md`.

What gets built:

| Step | Makes | Needs |
|---|---|---|
| shareware data | `data/DUKE3D.GRP` | curl, python3 |
| TRACE module | `module/duke3d.wasm` | [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-34) 34 |
| data pack | `door/duke3d.pak` | python3 |
| the door | `door/duke3ddoor.exe` | a mingw-w64 gcc with POSIX threads, make, patch |
| JPEG XL sound | `door/music/`, `door/sound/` | gcc, python3, ffmpeg with libvorbis (optional: without them the JPEG XL mode is silent) |

### On Windows (MSYS2)

1. Install [MSYS2](https://www.msys2.org/) and open the **MSYS2 MINGW64** shell from the Start menu (not "MSYS2 MSYS"
   or UCRT64: the commands below assume MINGW64).
2. Install the tools:

   ```
   pacman -S --needed make patch curl git mingw-w64-x86_64-gcc mingw-w64-x86_64-python mingw-w64-x86_64-ffmpeg
   ```

3. Unpack wasi-sdk for Windows (`wasi-sdk-34.0-x86_64-windows.tar.gz` from the release page above), for example to
   `C:\wasi-sdk`:

   ```
   mkdir -p /c/wasi-sdk && tar -xzf ~/Downloads/wasi-sdk-34.0-x86_64-windows.tar.gz -C /c/wasi-sdk --strip-components=1
   ```

4. Build (in the folder you cloned this repository to, e.g. `cd /c/src/Duke3DDoor-Win`):

   ```
   sh tools/get_shareware.sh
   make -C module WASI_SDK=/c/wasi-sdk && cp module/duke3d.wasm door/
   python3 tools/mkpak.py data/DUKE3D.GRP third_party/soundfont/TimGM6mb.sf2 door/duke3d.pak
   make -C door duke3ddoor.exe WIN64_CC=gcc
   make -C door music sound
   ```

   MSYS2's MINGW64 `gcc` is a mingw-w64 compiler with POSIX threads, so it stands in for the cross-compiler the
   Makefile names (`WIN64_CC=gcc`). The .exe is linked statically and needs no MSYS2 DLLs on the BBS machine.

### On Linux or WSL (cross-compiling)

1. Tools (Debian, Ubuntu, Mint; WSL's Ubuntu too):

   ```
   sudo apt install build-essential mingw-w64 python3 curl ffmpeg
   ```

2. Unpack wasi-sdk for Linux (`wasi-sdk-34.0-x86_64-linux.tar.gz`) into `~/tools`, which is where the module's
   Makefile looks (or give `WASI_SDK=<folder>`).
3. Build:

   ```
   sh tools/get_shareware.sh
   make -C module && cp module/duke3d.wasm door/
   python3 tools/mkpak.py data/DUKE3D.GRP third_party/soundfont/TimGM6mb.sf2 door/duke3d.pak
   make -C door win64          # door/duke3ddoor.exe (64-bit); make win32 makes an untested 32-bit one
   make -C door music sound
   ```

   Plain `make -C door` builds the Linux door from the same source.

`door/music/` and `door/sound/` are made from 3D Realms' data, so they aren't in the repository. `tools/test_windoor.py`
(Linux/WSL) tests the finished .exe without a BBS: `tools/winlaunch.c` plays Mystic for Windows, handing the door a
telnet socket in DOOR32.SYS.
