# Licensing for the DUKE NUKEM 3D door

Three things travel to the player, under three different licences.

## `duke3d.wasm` and `duke3ddoor` / `duke3ddoor.exe`: the game and the door, GNU GPL version 2

The game is **JFDuke3D** (github.com/jonof/jfduke3d, with jfbuild, jfaudiolib and jfmact), built from 3D Realms'
GPL release of the Duke Nukem 3D source and Ken Silverman's Build engine (`third_party/jfduke3d/GPL.TXT`; Ken's
BUILDLIC.TXT terms also apply to the Build engine). The music synth is **TinySoundFont** (MIT).

The door sends the compiled game (`duke3d.wasm`) to every TRACE player, so the obligation is **source**: players are
entitled to the source of what they were sent.

**The source is published at https://github.com/omniphil/Duke3DDoor**: the module, this door, the patches, and the
unmodified third-party code they are built from. Keep that repository up to date with the `duke3d.wasm` the door
actually hands out. The Windows door (`duke3ddoor.exe`) is published at
https://github.com/omniphil/Duke3DDoor-Win, which its exit screen gives.

Keep this file beside the door, so the licence travels with the game.

## The game data (`DUKE3D.GRP`, packed into `duke3d.pak`): 3D Realms' shareware terms

The shareware episode (v1.3D, "L.A. Meltdown") may be made available by BBSs with **250 or fewer nodes**, with every
file as released and **unmodified** (`../data/LICENSE.TXT`, [B] and [C]). `../tools/mkpak.py` only packs `DUKE3D.GRP`
byte for byte, and refuses any GRP that isn't the shareware one (md5 c03558e3a78d1c5356dc69b6134c5b55). It is not GPL,
so it is not in the repository; `../tools/get_shareware.sh` fetches it. `music/` and `sound/` are made from it, so
they aren't in the repository either.

**Never put the registered game's GRP (or Atomic Edition's) in this folder.** It may not be distributed, and the door
would send a copy to every player.

## The soundfont (`TimGM6mb.sf2`, packed into `duke3d.pak`): GNU GPL version 2

Tim Brechbill's TimGM6mb General MIDI soundfont, GPL-2, sent unchanged inside the pack. Its source is itself.
