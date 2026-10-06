/*
 * win32_game_stubs.c -- Windows only: jfaudiolib's driver table (drivers.c) always lists the WinMM driver on Windows,
 * which would play the game's MIDI on the BBS machine's own sound card. The door's copy of the game must never make
 * a sound there, so the WinMM slot is answered here with a driver that is never available; the game's sound goes
 * through the TRACE drivers (../module/src/audio_trace.c), as on Linux. (DirectSound and XAudio2 are left out of
 * drivers.c with NO_DIRECTSOUND and no HAVE_XAUDIO2; see the Makefile.)
 */

#ifdef _WIN32

#include "midifuncs.h"

enum { WinMM_Error = -1, WinMM_Unavailable = 1 };

int WinMMDrv_GetError(void) { return WinMM_Unavailable; }
const char *WinMMDrv_ErrorString(int ErrorNumber) { (void)ErrorNumber; return "WinMM isn't used by the door."; }

int  WinMMDrv_CD_Init(void) { return WinMM_Error; }
void WinMMDrv_CD_Shutdown(void) { }
int  WinMMDrv_CD_Play(int track, int loop) { (void)track; (void)loop; return WinMM_Error; }
void WinMMDrv_CD_Stop(void) { }
void WinMMDrv_CD_Pause(int pauseon) { (void)pauseon; }
int  WinMMDrv_CD_IsPlaying(void) { return 0; }
void WinMMDrv_CD_SetVolume(int volume) { (void)volume; }

int  WinMMDrv_MIDI_Init(midifuncs *funcs, const char *params) { (void)funcs; (void)params; return WinMM_Error; }
void WinMMDrv_MIDI_Shutdown(void) { }
int  WinMMDrv_MIDI_StartPlayback(void (*service)(void)) { (void)service; return WinMM_Error; }
void WinMMDrv_MIDI_HaltPlayback(void) { }
void WinMMDrv_MIDI_SetTempo(int tempo, int division) { (void)tempo; (void)division; }
void WinMMDrv_MIDI_Lock(void) { }
void WinMMDrv_MIDI_Unlock(void) { }

#endif
