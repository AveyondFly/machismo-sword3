#ifndef SWORD3_AUDIO_BRIDGE_H
#define SWORD3_AUDIO_BRIDGE_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sword3AudioBridge Sword3AudioBridge;
typedef struct Sword3AudioHandle Sword3AudioHandle;

typedef enum Sword3AudioOwnership {
	/* Attach to an already-open SDL_mixer device; close leaves it untouched. */
	SWORD3_AUDIO_ATTACH = 0,
	/* Open and later close SDL_mixer. SDL's audio subsystem remains caller-owned. */
	SWORD3_AUDIO_OPEN_AND_OWN = 1
} Sword3AudioOwnership;

typedef enum Sword3AudioKind {
	SWORD3_AUDIO_EFFECT = 0,
	SWORD3_AUDIO_MUSIC = 1
} Sword3AudioKind;

typedef struct Sword3AudioConfig {
	Sword3AudioOwnership ownership;
	int frequency;
	Uint16 format;
	int channels;
	int chunk_size;
	int mixer_init_flags;
} Sword3AudioConfig;

/*
 * SDL_INIT_AUDIO is always caller-owned. In ATTACH mode Mix_QuerySpec must
 * report an existing device. In OPEN_AND_OWN mode this bridge balances its
 * Mix_Init/Mix_OpenAudio calls during close. Calls on live handles are
 * serialized by the bridge. The caller must exclude concurrent handle/bridge
 * close, and must not manipulate SDL_mixer's process-global music channel
 * outside the bridge while it is active.
 */
Sword3AudioBridge *sword3_audio_bridge_open(const Sword3AudioConfig *config);
void sword3_audio_bridge_close(Sword3AudioBridge *bridge);

Sword3AudioHandle *sword3_audio_open_file(Sword3AudioBridge *bridge,
					 Sword3AudioKind kind,
					 const char *path);
Sword3AudioHandle *sword3_audio_open_memory(Sword3AudioBridge *bridge,
					   Sword3AudioKind kind,
					   const void *data, size_t size);
/*
 * Memory input is copied and kept on the handle until close. Mix_LoadMUS_RW
 * streams MP3 from that buffer for the whole lifetime of Mix_Music.
 */

/* loops follows SDL_mixer convention: zero plays once, -1 repeats forever. */
int sword3_audio_play(Sword3AudioHandle *handle, int loops);
void sword3_audio_stop(Sword3AudioHandle *handle);
/* Volume is clamped to 0..MIX_MAX_VOLUME. */
int sword3_audio_set_volume(Sword3AudioHandle *handle, int volume);
void sword3_audio_close(Sword3AudioHandle *handle);

#ifdef __cplusplus
}
#endif

#endif
