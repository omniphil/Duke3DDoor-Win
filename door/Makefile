# The DUKE NUKEM 3D door. For TERMinator it sends duke3d.wasm and duke3d.pak, which run on the player's own machine
# (see ../module). For every other terminal it runs the same game itself and sends it as JPEG XL pictures with sound
# (pix_*.c) or as ANSI (ansi_*.c), so the game's sources (JFDuke3D and ../module/src, its platform layer) are built
# in here too, natively, 64-bit. (The Wolfenstein 3D door's Makefile, for Duke, on Linux and Windows.)
#
#   make            Linux: duke3ddoor (for Mystic for Linux, or any BBS that runs DOOR32 doors)
#   make win64      Windows 64-bit: duke3ddoor.exe (for Mystic on Windows), cross-built with mingw-w64
#   make win32      Windows 32-bit: win32/duke3ddoor.exe, an untested extra (builds and links; never run)
#   make music      the JPEG XL mode's music, into music/ (needs ../data/DUKE3D.GRP and ffmpeg with libvorbis)
#   make sound      the JPEG XL mode's sound effects, into sound/ (the same)
#   make bundle     copies the game's sources into native/, so this folder builds on the BBS box on its own
#
# libjxl is loaded at run time (jxl_enc.c: libjxl.so, or jxl.dll beside the .exe), so only its headers are needed,
# and they're here. mingw-w64: sudo apt install mingw-w64. The -posix compilers are used (winpthreads: the game runs
# on its own thread); everything is linked statically so the .exe needs no mingw DLLs.

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -O2
LDFLAGS  = -pthread -lm -ldl

WIN64_CC = x86_64-w64-mingw32-gcc-posix
WIN32_CC = i686-w64-mingw32-gcc-posix
# C99 printf (%zu, %lld) rather than the old msvcrt's; Windows 7 and later
WIN_CFLAGS  = -Wall -Wextra -std=c11 -O2 -D__USE_MINGW_ANSI_STDIO=1 -D_WIN32_WINNT=0x0601 -Wno-format
WIN_LDFLAGS = -static -lws2_32 -lwinmm -lshell32 -lole32 -lm -lpthread

MAKEFLAGS += -r
.SUFFIXES:

# ---- the game's sources ----
# The development tree when there is one (so edits there are always what gets built), otherwise the copy
# `make bundle` puts in native/ for the BBS
MODULE        = $(firstword $(wildcard ../module/src/duketrace.c) $(wildcard native/module/src/duketrace.c))
MODULE_DIR    = $(patsubst %/src/duketrace.c,%,$(MODULE))
JFDUKE_SRC    = $(firstword $(wildcard ../third_party/jfduke3d/src/game.c native/jfduke3d/src/game.c))
JFDUKE_TOP    = $(patsubst %/src/game.c,%,$(JFDUKE_SRC))
TSF           = $(firstword $(wildcard ../third_party/TinySoundFont native/TinySoundFont))
PATCH_DIR     = $(firstword $(wildcard ../patches native/patches))
TRACE_INCLUDE = $(firstword $(wildcard ../../../TERMinator-Windows/trace/include ../module/trace native/trace))

# JFDuke3D is built from a patched copy: the same patches as the module (see ../module/Makefile), then the door's own
# (patches/ here: hooks for its ANSI mode, which the TRACE module doesn't have)
JFDUKE  = obj/jfduke3d
PATCHES = $(sort $(wildcard $(PATCH_DIR)/*.patch)) $(sort $(wildcard patches/*.patch))

# The game's calls into its sound library and its settings, caught by pix_hooks.c on the way (linked with --wrap):
# the JPEG XL mode plays the same sounds on the caller's terminal, and the door can set the view size
WRAP = FX_PlayAuto3D FX_PlayLoopedAuto FX_Pan3D FX_StopSound FX_StopAllSounds FX_SetVolume FX_SetReverseStereo \
       MUSIC_PlaySong MUSIC_StopSong MUSIC_Pause MUSIC_Continue MUSIC_SetVolume CONFIG_ReadSetup
WRAP_FLAGS = $(foreach f, $(WRAP), -Wl,--wrap=$(f))

# The same files and flags ../module/Makefile builds into duke3d.wasm, only for a normal CPU: classic renderer, no
# assembly, little-endian, and the module's file layer forced in (duketrace_compat.h). On Windows, jfaudiolib's
# DirectSound driver is left out (and WinMM answered by win32_game_stubs.c): the game never plays sound itself.
GAME_DEFS = -DNDEBUG -DRENDERTYPETRACE=1 -fno-strict-aliasing -DB_LITTLE_ENDIAN=1 -DB_BIG_ENDIAN=0 \
            -DB_ENDIAN_C_INLINE=1 -include duketrace_compat.h -I $(MODULE_DIR)/include -I $(MODULE_DIR)/src \
            -I $(TRACE_INCLUDE) -I $(JFDUKE)/src -I $(JFDUKE)/jfbuild/include -I $(JFDUKE)/jfbuild/src \
            -I $(JFDUKE)/jfmact -I $(JFDUKE)/jfaudiolib/include -I $(JFDUKE)/jfaudiolib/src -I $(TSF) -I .
GAME_CFLAGS     = -O2 -pthread -std=gnu99 -w $(GAME_DEFS)
WIN_GAME_CFLAGS = -O2 -std=gnu99 -w -D__USE_MINGW_ANSI_STDIO=1 -D_WIN32_WINNT=0x0601 -DNO_DIRECTSOUND $(GAME_DEFS)

GAME_SRC   = game.c actors.c gamedef.c global.c menues.c player.c premap.c sector.c rts.c config.c osdfuncs.c \
             osdcmds.c grpscan.c sounds.c version.c
MACT_SRC   = util_lib.c file_lib.c control.c keyboard.c mouse.c mathutil.c scriplib.c animlib.c
ENGINE_SRC = a-c.c baselayer.c cache1d.c compat.c crc32.c defs.c engine.c kplib.c mmulti_null.c osd.c pragmas.c \
             scriptfile.c textfont.c talltextfont.c smalltextfont.c startwin_stub.c version.c
AUDIO_SRC  = drivers.c fx_man.c cd.c multivoc.c mix.c mixst.c pitch.c music.c midi.c driver_nosound.c asssys.c
TRACE_SRC  = duketrace.c tracelayer.c audio_trace.c file_trace.c
# The door's own files that read the game's state, built with its headers and flags
HOOK_SRC   = pix_hooks.c ansi_hud.c

# obj/<target>/game/<part>/<file>.o: the parts as the module has them (two version.c, in game and engine)
game_objs = $(patsubst %.c, obj/$(1)/game/game/%.o,   $(GAME_SRC)) \
            $(patsubst %.c, obj/$(1)/game/mact/%.o,   $(MACT_SRC)) \
            $(patsubst %.c, obj/$(1)/game/engine/%.o, $(ENGINE_SRC)) \
            $(patsubst %.c, obj/$(1)/game/audio/%.o,  $(AUDIO_SRC)) \
            $(patsubst %.c, obj/$(1)/game/trace/%.o,  $(TRACE_SRC)) \
            $(patsubst %.c, obj/$(1)/game/hooks/%.o,  $(HOOK_SRC))

# ---- the door ----
# Everything but the platform layer is the same on both
COMMON  = main.c door.c trace_duke.c trace_door.c files.c sha256.c apc.c jxl_enc.c ansi_pace.c telnet.c \
          ansi_host.c ansi_screen.c ansi_input.c ansi_play.c pix_play.c pix_sound.c
LINUX_SRC = $(COMMON) platform_posix.c
WIN_SRC   = $(COMMON) platform_win32.c win32_game_stubs.c

LINUX_OBJ = $(patsubst %.c, obj/linux/%.o, $(LINUX_SRC)) $(call game_objs,linux)
WIN64_OBJ = $(patsubst %.c, obj/win64/%.o, $(WIN_SRC)) $(call game_objs,win64)
WIN32_OBJ = $(patsubst %.c, obj/win32/%.o, $(WIN_SRC)) $(call game_objs,win32)
HEADERS   = $(wildcard *.h)

duke3ddoor: $(LINUX_OBJ)
	$(CC) $(LINUX_OBJ) -o $@ $(LDFLAGS) $(WRAP_FLAGS)

win64: duke3ddoor.exe
win32: win32/duke3ddoor.exe

duke3ddoor.exe: $(WIN64_OBJ)
	$(WIN64_CC) $(WIN64_OBJ) -o $@ $(WRAP_FLAGS) $(WIN_LDFLAGS)

win32/duke3ddoor.exe: $(WIN32_OBJ)
	@mkdir -p win32
	$(WIN32_CC) $(WIN32_OBJ) -o $@ $(WRAP_FLAGS) $(WIN_LDFLAGS)

# The door's files. ansi_host.c includes trace_api.h, which marks its functions for WebAssembly (-Wno-attributes).
# trace_door.c is an exact copy of BBSGames/TraceDoor's (refreshed by its sync.sh); win32_compat.h gives it the two
# POSIX calls Windows lacks. win32_game_stubs.c needs jfaudiolib's headers.
DOOR_INC = -I jxl_include -I $(TRACE_INCLUDE) -Wno-attributes -I $(JFDUKE)/jfaudiolib/src

obj/linux/%.o: %.c $(HEADERS) | $(JFDUKE)/.patched
	@mkdir -p obj/linux
	$(CC) $(CFLAGS) $(DOOR_INC) -c $< -o $@

obj/win64/%.o: %.c $(HEADERS) | $(JFDUKE)/.patched
	@mkdir -p obj/win64
	$(WIN64_CC) $(WIN_CFLAGS) $(DOOR_INC) $(if $(filter trace_door.c,$<),-include win32_compat.h) -c $< -o $@

obj/win32/%.o: %.c $(HEADERS) | $(JFDUKE)/.patched
	@mkdir -p obj/win32
	$(WIN32_CC) $(WIN_CFLAGS) $(DOOR_INC) $(if $(filter trace_door.c,$<),-include win32_compat.h) -c $< -o $@

# The game, for each target: $(1) target, $(2) compiler, $(3) flags
define GAME_RULES
obj/$(1)/game/game/%.o: $(JFDUKE)/src/%.c | $(JFDUKE)/.patched
	@mkdir -p $$(@D)
	$(2) $(3) -c $$< -o $$@
obj/$(1)/game/mact/%.o: $(JFDUKE)/jfmact/%.c | $(JFDUKE)/.patched
	@mkdir -p $$(@D)
	$(2) $(3) -c $$< -o $$@
obj/$(1)/game/engine/%.o: $(JFDUKE)/jfbuild/src/%.c | $(JFDUKE)/.patched
	@mkdir -p $$(@D)
	$(2) $(3) -c $$< -o $$@
# drivers.c's "SDL" and "FluidSynth" slots are the TRACE drivers (audio_trace.c): the names are only the slots
obj/$(1)/game/audio/%.o: $(JFDUKE)/jfaudiolib/src/%.c | $(JFDUKE)/.patched
	@mkdir -p $$(@D)
	$(2) $(3) -DHAVE_SDL=2 -DHAVE_FLUIDSYNTH -c $$< -o $$@
obj/$(1)/game/trace/%.o: $(MODULE_DIR)/src/%.c $(MODULE_DIR)/src/duketrace.h | $(JFDUKE)/.patched
	@mkdir -p $$(@D)
	$(2) $(3) -c $$< -o $$@
obj/$(1)/game/hooks/%.o: %.c $(HEADERS) | $(JFDUKE)/.patched
	@mkdir -p $$(@D)
	$(2) $(3) -c $$< -o $$@
endef
$(eval $(call GAME_RULES,linux,$(CC),$(GAME_CFLAGS)))
$(eval $(call GAME_RULES,win64,$(WIN64_CC),$(WIN_GAME_CFLAGS)))
$(eval $(call GAME_RULES,win32,$(WIN32_CC),$(WIN_GAME_CFLAGS)))

# The patched copy: made (or remade, when a patch changes) before anything is compiled from it
$(JFDUKE)/src/%.c $(JFDUKE)/jfmact/%.c $(JFDUKE)/jfbuild/src/%.c $(JFDUKE)/jfaudiolib/src/%.c: $(JFDUKE)/.patched ;

# ⚠️ Without this make counts the patched copy's .c files as intermediate (they come from a pattern rule) and deletes
# them at the end of every build
.PRECIOUS: $(JFDUKE)/src/%.c $(JFDUKE)/jfmact/%.c $(JFDUKE)/jfbuild/src/%.c $(JFDUKE)/jfaudiolib/src/%.c

$(JFDUKE)/.patched: $(PATCHES)
	@# Dropbox can hold a just-written folder for a moment, so the first rm may fail: try again after a second
	rm -rf $(JFDUKE) || (sleep 2 && rm -rf $(JFDUKE))
	mkdir -p $(JFDUKE)/jfbuild $(JFDUKE)/jfaudiolib
	cp -r $(JFDUKE_TOP)/src $(JFDUKE_TOP)/jfmact $(JFDUKE)/
	cp -r $(JFDUKE_TOP)/jfbuild/src $(JFDUKE_TOP)/jfbuild/include $(JFDUKE)/jfbuild/
	cp -r $(JFDUKE_TOP)/jfaudiolib/src $(JFDUKE_TOP)/jfaudiolib/include $(JFDUKE)/jfaudiolib/
	for p in $(PATCHES); do patch -s -d $(JFDUKE) -p1 < $$p || exit 1; done
	touch $@

# ---- the JPEG XL mode's sound, made from the game's data (so it ships with the door but isn't in the public source).
# Needs the data (../tools/get_shareware.sh, or GRP=<path to DUKE3D.GRP>) and ffmpeg with libvorbis.
GRP       ?= ../data/DUKE3D.GRP
SF2       ?= $(firstword $(wildcard ../third_party/soundfont/TimGM6mb.sf2 native/soundfont/TimGM6mb.sf2))
MUSIC_TMP ?= /tmp/dukemusic

midirender: tools/midirender.c sha256.c
	$(CC) -O2 -Wall -I $(TSF) tools/midirender.c sha256.c -o $@ -lm

music: midirender
	@[ -f $(GRP) ] || { echo "make music: no $(GRP) -- run tools/get_shareware.sh in the top folder, or give GRP=<path to DUKE3D.GRP>"; exit 1; }
	@command -v ffmpeg > /dev/null || { echo "make music: needs ffmpeg with libvorbis (Debian, Ubuntu, Mint, Raspberry Pi OS: sudo apt install ffmpeg)"; exit 1; }
	@ffmpeg -hide_banner -encoders 2> /dev/null | grep -q libvorbis || { echo "make music: this ffmpeg has no libvorbis encoder"; exit 1; }
	./midirender $(GRP) $(SF2) $(MUSIC_TMP)
	python3 tools/make_music.py $(MUSIC_TMP) music

sound:
	@[ -f $(GRP) ] || { echo "make sound: no $(GRP) -- run tools/get_shareware.sh in the top folder, or give GRP=<path to DUKE3D.GRP>"; exit 1; }
	python3 tools/make_sfx.py $(GRP) sound

# Everything the sysop needs in one folder: the door, the game and its data. On the BBS these usually came along with
# the folder already, so this only fills in what's missing.
install: duke3ddoor
	@if [ -f duke3d.wasm ]; then echo "duke3d.wasm: already here"; \
	elif [ -f ../module/duke3d.wasm ]; then cp ../module/duke3d.wasm . && echo "duke3d.wasm: copied"; \
	else echo "duke3d.wasm: MISSING -- copy it in from the development machine"; fi
	@if [ -f duke3d.pak ]; then echo "duke3d.pak: already here"; \
	elif [ -f ../data/DUKE3D.GRP ]; then python3 ../tools/mkpak.py ../data/DUKE3D.GRP ../third_party/soundfont/TimGM6mb.sf2 duke3d.pak; \
	else echo "duke3d.pak: MISSING -- copy it in from the development machine"; fi
	@[ -f music/index.txt ] && echo "music/: here" || echo "music/: MISSING -- the JPEG XL mode will have no music (make music)"
	@[ -f sound/index.txt ] && echo "sound/: here" || echo "sound/: MISSING -- the JPEG XL mode will have no sound (make sound)"
	@mkdir -p saves && echo "saves/: ready"

# Copies the game's sources into native/, so this folder builds on the BBS box on its own (run on the development
# machine before copying the folder over). Only what the build uses.
bundle:
	rm -rf native
	mkdir -p native/module native/jfduke3d/jfbuild native/jfduke3d/jfaudiolib native/TinySoundFont native/patches \
	         native/trace
	cp -r ../module/src ../module/include native/module/
	cp -r ../third_party/jfduke3d/src ../third_party/jfduke3d/jfmact ../third_party/jfduke3d/GPL.TXT \
	      ../third_party/jfduke3d/README.md native/jfduke3d/
	cp -r ../third_party/jfduke3d/jfbuild/src ../third_party/jfduke3d/jfbuild/include native/jfduke3d/jfbuild/
	cp -r ../third_party/jfduke3d/jfaudiolib/src ../third_party/jfduke3d/jfaudiolib/include native/jfduke3d/jfaudiolib/
	cp ../third_party/TinySoundFont/tsf.h ../third_party/TinySoundFont/LICENSE native/TinySoundFont/ 2>/dev/null || \
	    cp ../third_party/TinySoundFont/tsf.h native/TinySoundFont/
	for p in ../patches/*.patch; do [ -f "$$p" ] && cp "$$p" native/patches/; done; true
	cp $(TRACE_INCLUDE)/trace_api.h native/trace/
	@du -sh native
	@[ -f music/index.txt ] || echo "note: no music/ yet -- run 'make music' first, or the JPEG XL mode has no music"
	@[ -f sound/index.txt ] || echo "note: no sound/ yet -- run 'make sound' first, or the JPEG XL mode has no sound"

# The telnet layer's own test (it runs on Linux too)
test-telnet: tools/test_telnet.c telnet.c telnet.h
	$(CC) $(CFLAGS) -I . tools/test_telnet.c telnet.c -o obj/test_telnet && ./obj/test_telnet

clean:
	rm -rf obj duke3ddoor duke3ddoor.exe win32 midirender

.PHONY: clean install music sound win64 win32 test-telnet bundle
