# Builds duke3d.wasm: the whole of Duke Nukem 3D (game, classic software renderer, digitised sound, General MIDI
# music through TinySoundFont) as a TRACE module the door sends to TERMinator.
#
# JFDuke3D (with jfbuild, jfmact and jfaudiolib) comes from ../third_party unmodified. Its platform layer (jfbuild's
# sdlayer2.c) is replaced by src/tracelayer.c, its sound drivers by src/audio_trace.c and its files by src/file_trace.c.
# Run in WSL/Linux with wasi-sdk.
# No built-in rules: with the patched-copy rule below they would chain into nonsense
MAKEFLAGS += -r
.SUFFIXES:

WASI_SDK ?= $(HOME)/tools/wasi-sdk-34.0-x86_64-linux
CC  = $(WASI_SDK)/bin/clang

# Built from a patched copy (obj/jfduke3d): ../third_party stays exactly as released, and ../patches holds every
# change made to it (see ../README.md "The patches")
JFDUKE_SRC = ../third_party/jfduke3d
JFDUKE     = obj/jfduke3d
PATCHES    = $(sort $(wildcard ../patches/*.patch))
TSF        = ../third_party/TinySoundFont
# trace_api.h: the client's own tree when building here, or the copy bundled with the published source
TRACE_INCLUDE = $(firstword $(wildcard ../../../TERMinator-Windows/trace/include) trace)

# Classic renderer only: no Polymost, no OpenGL, no assembly. RENDERTYPETRACE names no layer the game knows, so no
# WebAssembly is little-endian; compat.h only knows the usual platforms, so its B_* endian settings are given here.
# start-up window either.
COMMON = --target=wasm32-wasip1-threads -O2 -pthread -DNDEBUG -DRENDERTYPETRACE=1 \
         -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_GETPID -D_WASI_EMULATED_MMAN \
         -fno-strict-aliasing -DB_LITTLE_ENDIAN=1 -DB_BIG_ENDIAN=0 -DB_ENDIAN_C_INLINE=1 \
         -include duketrace_compat.h -I include -I src -I $(TRACE_INCLUDE) \
         -I $(JFDUKE)/src -I $(JFDUKE)/jfbuild/include -I $(JFDUKE)/jfbuild/src -I $(JFDUKE)/jfmact \
         -I $(JFDUKE)/jfaudiolib/include -I $(JFDUKE)/jfaudiolib/src -I $(TSF)
CFLAGS  = $(COMMON) -std=gnu99 -w $(CFLAGS_EXTRA)
LDFLAGS = -mexec-model=reactor -Wl,--import-memory,--export-memory,--shared-memory,--max-memory=536870912 \
          -Wl,--export=wasi_thread_start -Wl,--strip-all -Wl,-z,stack-size=1048576 \
          -lwasi-emulated-signal -lwasi-emulated-process-clocks -lwasi-emulated-getpid -lwasi-emulated-mman

GAME_SRC   = game.c actors.c gamedef.c global.c menues.c player.c premap.c sector.c rts.c config.c osdfuncs.c \
             osdcmds.c grpscan.c sounds.c version.c
MACT_SRC   = util_lib.c file_lib.c control.c keyboard.c mouse.c mathutil.c scriplib.c animlib.c
ENGINE_SRC = a-c.c baselayer.c cache1d.c compat.c crc32.c defs.c engine.c kplib.c mmulti_null.c osd.c pragmas.c \
             scriptfile.c textfont.c talltextfont.c smalltextfont.c startwin_stub.c version.c
AUDIO_SRC  = drivers.c fx_man.c cd.c multivoc.c mix.c mixst.c pitch.c music.c midi.c driver_nosound.c asssys.c
TRACE_SRC  = $(notdir $(wildcard src/*.c))

OBJ = $(patsubst %.c, obj/game/%.o,   $(GAME_SRC)) \
      $(patsubst %.c, obj/mact/%.o,   $(MACT_SRC)) \
      $(patsubst %.c, obj/engine/%.o, $(ENGINE_SRC)) \
      $(patsubst %.c, obj/audio/%.o,  $(AUDIO_SRC)) \
      $(patsubst %.c, obj/trace/%.o,  $(TRACE_SRC))

duke3d.wasm: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LDFLAGS) -o $@

obj/game/%.o: $(JFDUKE)/src/%.c | obj/game
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
obj/mact/%.o: $(JFDUKE)/jfmact/%.c | obj/mact
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
obj/engine/%.o: $(JFDUKE)/jfbuild/src/%.c | obj/engine
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
# drivers.c's "SDL" and "FluidSynth" slots are the TRACE drivers (src/audio_trace.c): the names are only the slots
obj/audio/%.o: $(JFDUKE)/jfaudiolib/src/%.c | obj/audio
	$(CC) $(CFLAGS) -DHAVE_SDL=2 -DHAVE_FLUIDSYNTH -MMD -MP -c $< -o $@
obj/trace/%.o: src/%.c $(JFDUKE)/.patched | obj/trace
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# The patched copy: made (or remade, when a patch or the source changes) before anything is compiled from it
$(JFDUKE)/src/%.c $(JFDUKE)/jfmact/%.c $(JFDUKE)/jfbuild/src/%.c $(JFDUKE)/jfaudiolib/src/%.c: $(JFDUKE)/.patched ;

# ⚠️ Without this make counts the patched copy's .c files as intermediate (they come from a pattern rule) and deletes
# them at the end of every build
.PRECIOUS: $(JFDUKE)/src/%.c $(JFDUKE)/jfmact/%.c $(JFDUKE)/jfbuild/src/%.c $(JFDUKE)/jfaudiolib/src/%.c

$(JFDUKE)/.patched: $(PATCHES) $(wildcard $(JFDUKE_SRC)/src/*.[ch] $(JFDUKE_SRC)/jfbuild/src/*.[ch] \
                    $(JFDUKE_SRC)/jfbuild/include/*.h $(JFDUKE_SRC)/jfmact/*.[ch] $(JFDUKE_SRC)/jfaudiolib/src/*.[ch] \
                    $(JFDUKE_SRC)/jfaudiolib/include/*.h) | obj
	@# Dropbox can hold a just-written folder for a moment, so the first rm may fail: try again after a second
	rm -rf $(JFDUKE) || (sleep 2 && rm -rf $(JFDUKE))
	mkdir -p $(JFDUKE)/jfbuild $(JFDUKE)/jfaudiolib
	cp -r $(JFDUKE_SRC)/src $(JFDUKE_SRC)/jfmact $(JFDUKE)/
	cp -r $(JFDUKE_SRC)/jfbuild/src $(JFDUKE_SRC)/jfbuild/include $(JFDUKE)/jfbuild/
	cp -r $(JFDUKE_SRC)/jfaudiolib/src $(JFDUKE_SRC)/jfaudiolib/include $(JFDUKE)/jfaudiolib/
	for p in $(PATCHES); do patch -s -d $(JFDUKE) -p1 < $$p || exit 1; done
	touch $@

-include $(wildcard obj/*/*.d)

obj obj/game obj/mact obj/engine obj/audio obj/trace:
	mkdir -p $@

clean:
	rm -rf obj duke3d.wasm

.PHONY: clean
