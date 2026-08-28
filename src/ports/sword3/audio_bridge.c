#include "audio_bridge.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct Sword3AudioHandle {
	Sword3AudioBridge *bridge;
	Sword3AudioKind kind;
	union {
		Mix_Chunk *chunk;
		Mix_Music *music;
	} media;
	int volume;
	struct Sword3AudioHandle *next;
};

struct Sword3AudioBridge {
	SDL_mutex *mutex;
	Sword3AudioHandle *handles;
	Sword3AudioHandle *current_music;
	SDL_bool owns_audio;
	SDL_bool mixer_init_called;
};

static SDL_RWops *rw_from_copy(const void *data, size_t size, void **copy_out)
{
	void *copy;
	SDL_RWops *rw;

	if (!data || !size || size > INT_MAX) {
		errno = EINVAL;
		SDL_SetError("invalid in-memory audio size");
		return NULL;
	}
	copy = malloc(size ? size : 1);
	if (!copy) {
		errno = ENOMEM;
		SDL_SetError("out of memory copying audio");
		return NULL;
	}
	if (size)
		memcpy(copy, data, size);
	rw = SDL_RWFromConstMem(copy, (int)size);
	if (!rw) {
		free(copy);
		return NULL;
	}
	*copy_out = copy;
	return rw;
}

Sword3AudioBridge *sword3_audio_bridge_open(const Sword3AudioConfig *config)
{
	Sword3AudioBridge *bridge;
	Sword3AudioConfig defaults = {
		SWORD3_AUDIO_ATTACH,
		44100,
		MIX_DEFAULT_FORMAT,
		2,
		2048,
		0
	};
	int initialized;

	if (!config)
		config = &defaults;
	if (config->ownership != SWORD3_AUDIO_ATTACH &&
	    config->ownership != SWORD3_AUDIO_OPEN_AND_OWN) {
		errno = EINVAL;
		SDL_SetError("invalid Sword3 audio ownership");
		return NULL;
	}
	if (config->ownership == SWORD3_AUDIO_OPEN_AND_OWN &&
	    (config->frequency <= 0 || config->channels <= 0 ||
	     config->chunk_size <= 0)) {
		errno = EINVAL;
		SDL_SetError("invalid Sword3 audio device configuration");
		return NULL;
	}
	bridge = calloc(1, sizeof(*bridge));
	if (!bridge) {
		errno = ENOMEM;
		SDL_SetError("out of memory creating audio bridge");
		return NULL;
	}
	bridge->mutex = SDL_CreateMutex();
	if (!bridge->mutex)
		goto fail;

	if (config->ownership == SWORD3_AUDIO_OPEN_AND_OWN) {
		if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
			SDL_SetError("SDL audio subsystem is not initialized");
			goto fail;
		}
		initialized = Mix_Init(config->mixer_init_flags);
		bridge->mixer_init_called = SDL_TRUE;
		if ((initialized & config->mixer_init_flags) !=
		    config->mixer_init_flags) {
			SDL_SetError("SDL_mixer codec initialization failed: %s",
				     Mix_GetError());
			goto fail;
		}
		if (Mix_OpenAudio(config->frequency, config->format,
				  config->channels, config->chunk_size) < 0)
			goto fail;
		bridge->owns_audio = SDL_TRUE;
	} else if (!Mix_QuerySpec(NULL, NULL, NULL)) {
		SDL_SetError("SDL_mixer audio device is not open");
		goto fail;
	}
	return bridge;

fail:
	if (bridge->owns_audio)
		Mix_CloseAudio();
	if (bridge->mixer_init_called)
		Mix_Quit();
	if (bridge->mutex)
		SDL_DestroyMutex(bridge->mutex);
	free(bridge);
	return NULL;
}

static Sword3AudioHandle *allocate_handle(Sword3AudioBridge *bridge,
					 Sword3AudioKind kind)
{
	Sword3AudioHandle *handle;

	if (!bridge ||
	    (kind != SWORD3_AUDIO_EFFECT && kind != SWORD3_AUDIO_MUSIC)) {
		errno = EINVAL;
		SDL_SetError("invalid Sword3 audio handle arguments");
		return NULL;
	}
	handle = calloc(1, sizeof(*handle));
	if (!handle) {
		errno = ENOMEM;
		SDL_SetError("out of memory creating audio handle");
		return NULL;
	}
	handle->bridge = bridge;
	handle->kind = kind;
	handle->volume = MIX_MAX_VOLUME;
	return handle;
}

static void link_handle_locked(Sword3AudioHandle *handle)
{
	Sword3AudioBridge *bridge = handle->bridge;

	handle->next = bridge->handles;
	bridge->handles = handle;
}

Sword3AudioHandle *sword3_audio_open_file(Sword3AudioBridge *bridge,
					 Sword3AudioKind kind,
					 const char *path)
{
	Sword3AudioHandle *handle;

	if (!path) {
		errno = EINVAL;
		SDL_SetError("audio path is null");
		return NULL;
	}
	handle = allocate_handle(bridge, kind);
	if (!handle)
		return NULL;
	SDL_LockMutex(bridge->mutex);
	if (kind == SWORD3_AUDIO_EFFECT)
		handle->media.chunk = Mix_LoadWAV(path);
	else
		handle->media.music = Mix_LoadMUS(path);
	if ((kind == SWORD3_AUDIO_EFFECT && !handle->media.chunk) ||
	    (kind == SWORD3_AUDIO_MUSIC && !handle->media.music)) {
		SDL_UnlockMutex(bridge->mutex);
		free(handle);
		return NULL;
	}
	link_handle_locked(handle);
	SDL_UnlockMutex(bridge->mutex);
	return handle;
}

Sword3AudioHandle *sword3_audio_open_memory(Sword3AudioBridge *bridge,
					   Sword3AudioKind kind,
					   const void *data, size_t size)
{
	Sword3AudioHandle *handle = allocate_handle(bridge, kind);
	void *memory_copy = NULL;
	SDL_RWops *rw;

	if (!handle)
		return NULL;
	rw = rw_from_copy(data, size, &memory_copy);
	if (!rw) {
		free(handle);
		return NULL;
	}
	SDL_LockMutex(bridge->mutex);
	if (kind == SWORD3_AUDIO_EFFECT)
		handle->media.chunk = Mix_LoadWAV_RW(rw, 1);
	else
		handle->media.music = Mix_LoadMUS_RW(rw, 1);
	/*
	 * With freesrc=1 SDL_mixer closes the RWops before returning and has
	 * already copied everything it needs, so its backing memory ends here.
	 */
	free(memory_copy);
	if ((kind == SWORD3_AUDIO_EFFECT && !handle->media.chunk) ||
	    (kind == SWORD3_AUDIO_MUSIC && !handle->media.music)) {
		SDL_UnlockMutex(bridge->mutex);
		free(handle);
		return NULL;
	}
	link_handle_locked(handle);
	SDL_UnlockMutex(bridge->mutex);
	return handle;
}

int sword3_audio_play(Sword3AudioHandle *handle, int loops)
{
	Sword3AudioBridge *bridge;
	int rc;

	if (!handle || loops < -1) {
		errno = EINVAL;
		SDL_SetError("invalid Sword3 audio play arguments");
		return -1;
	}
	bridge = handle->bridge;
	SDL_LockMutex(bridge->mutex);
	if (handle->kind == SWORD3_AUDIO_EFFECT) {
		Mix_VolumeChunk(handle->media.chunk, handle->volume);
		rc = Mix_PlayChannel(-1, handle->media.chunk, loops);
	} else {
		Mix_VolumeMusic(handle->volume);
		rc = Mix_PlayMusic(handle->media.music, loops);
		if (rc == 0)
			bridge->current_music = handle;
	}
	SDL_UnlockMutex(bridge->mutex);
	return rc < 0 ? -1 : 0;
}

static void stop_locked(Sword3AudioHandle *handle)
{
	Sword3AudioBridge *bridge = handle->bridge;

	if (handle->kind == SWORD3_AUDIO_EFFECT) {
		int channel_count = Mix_AllocateChannels(-1);

		/*
		 * One handle can be played repeatedly on several channels. Halt
		 * every channel that still references this chunk before freeing it.
		 */
		for (int channel = 0; channel < channel_count; channel++) {
			if (Mix_GetChunk(channel) == handle->media.chunk)
				Mix_HaltChannel(channel);
		}
	} else if (bridge->current_music == handle) {
		Mix_HaltMusic();
		bridge->current_music = NULL;
	}
}

void sword3_audio_stop(Sword3AudioHandle *handle)
{
	if (!handle)
		return;
	SDL_LockMutex(handle->bridge->mutex);
	stop_locked(handle);
	SDL_UnlockMutex(handle->bridge->mutex);
}

int sword3_audio_set_volume(Sword3AudioHandle *handle, int volume)
{
	if (!handle) {
		errno = EINVAL;
		return -1;
	}
	if (volume < 0)
		volume = 0;
	if (volume > MIX_MAX_VOLUME)
		volume = MIX_MAX_VOLUME;
	SDL_LockMutex(handle->bridge->mutex);
	handle->volume = volume;
	if (handle->kind == SWORD3_AUDIO_EFFECT) {
		Mix_VolumeChunk(handle->media.chunk, volume);
	} else if (handle->bridge->current_music == handle) {
		Mix_VolumeMusic(volume);
	}
	SDL_UnlockMutex(handle->bridge->mutex);
	return volume;
}

static void free_handle_locked(Sword3AudioHandle *handle)
{
	stop_locked(handle);
	if (handle->kind == SWORD3_AUDIO_EFFECT)
		Mix_FreeChunk(handle->media.chunk);
	else
		Mix_FreeMusic(handle->media.music);
	free(handle);
}

void sword3_audio_close(Sword3AudioHandle *handle)
{
	Sword3AudioBridge *bridge;
	Sword3AudioHandle **link;

	if (!handle)
		return;
	bridge = handle->bridge;
	SDL_LockMutex(bridge->mutex);
	link = &bridge->handles;
	while (*link && *link != handle)
		link = &(*link)->next;
	if (*link == handle) {
		*link = handle->next;
		free_handle_locked(handle);
	}
	SDL_UnlockMutex(bridge->mutex);
}

void sword3_audio_bridge_close(Sword3AudioBridge *bridge)
{
	Sword3AudioHandle *handle;

	if (!bridge)
		return;
	SDL_LockMutex(bridge->mutex);
	while ((handle = bridge->handles) != NULL) {
		bridge->handles = handle->next;
		free_handle_locked(handle);
	}
	SDL_UnlockMutex(bridge->mutex);
	if (bridge->owns_audio)
		Mix_CloseAudio();
	if (bridge->mixer_init_called)
		Mix_Quit();
	SDL_DestroyMutex(bridge->mutex);
	free(bridge);
}
