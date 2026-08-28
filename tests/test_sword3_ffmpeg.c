#include "ports/sword3/audio_bridge.h"
#include "ports/sword3/input.h"
#include "ports/sword3/video_bridge.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", \
			__FILE__, __LINE__, #condition); \
		goto fail; \
	} \
} while (0)

static void write_le16(unsigned char *out, unsigned int value)
{
	out[0] = (unsigned char)value;
	out[1] = (unsigned char)(value >> 8);
}

static void write_le32(unsigned char *out, uint32_t value)
{
	out[0] = (unsigned char)value;
	out[1] = (unsigned char)(value >> 8);
	out[2] = (unsigned char)(value >> 16);
	out[3] = (unsigned char)(value >> 24);
}

static unsigned char *make_wav(size_t *size_out)
{
	const size_t samples = 8000;
	unsigned char *wav = malloc(44 + samples);

	if (!wav)
		return NULL;
	memcpy(wav, "RIFF", 4);
	write_le32(wav + 4, (uint32_t)(36 + samples));
	memcpy(wav + 8, "WAVEfmt ", 8);
	write_le32(wav + 16, 16);
	write_le16(wav + 20, 1);
	write_le16(wav + 22, 1);
	write_le32(wav + 24, 8000);
	write_le32(wav + 28, 8000);
	write_le16(wav + 32, 1);
	write_le16(wav + 34, 8);
	memcpy(wav + 36, "data", 4);
	write_le32(wav + 40, (uint32_t)samples);
	memset(wav + 44, 128, samples);
	*size_out = 44 + samples;
	return wav;
}

static int test_sdl_services(void)
{
	Sword3Input *input = NULL;
	Sword3AudioBridge *audio = NULL;
	Sword3AudioHandle *effect = NULL;
	Sword3AudioHandle *music = NULL;
	Sword3InputEvent input_event;
	SDL_Event event;
	unsigned char *wav = NULL;
	size_t wav_size = 0;
	Sword3AudioConfig config = {
		SWORD3_AUDIO_OPEN_AND_OWN,
		44100,
		MIX_DEFAULT_FORMAT,
		2,
		1024,
		0
	};
	int rc = -1;

	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0)
		goto done;
	input = sword3_input_create(NULL, NULL);
	if (!input)
		goto done;

	memset(&event, 0, sizeof(event));
	event.type = SDL_KEYDOWN;
	event.key.keysym.scancode = SDL_SCANCODE_A;
	if (sword3_input_handle_event(input, &event) != 1 ||
	    sword3_input_handle_event(input, &event) != 1 ||
	    !sword3_input_is_down(input, SDL_SCANCODE_A))
		goto done;
	event.type = SDL_KEYUP;
	if (sword3_input_handle_event(input, &event) != 1 ||
	    sword3_input_is_down(input, SDL_SCANCODE_A))
		goto done;
	if (sword3_input_poll(input, &input_event) != 1 ||
	    input_event.scancode != SDL_SCANCODE_A || !input_event.pressed ||
	    sword3_input_poll(input, &input_event) != 1 ||
	    input_event.scancode != SDL_SCANCODE_A || input_event.pressed ||
	    sword3_input_poll(input, &input_event) != 0)
		goto done;

	event.type = SDL_KEYDOWN;
	if (sword3_input_handle_event(input, &event) != 1)
		goto done;
	memset(&event, 0, sizeof(event));
	event.type = SDL_WINDOWEVENT;
	event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
	if (sword3_input_handle_event(input, &event) != 1 ||
	    sword3_input_is_down(input, SDL_SCANCODE_A))
		goto done;
	if (sword3_input_poll(input, &input_event) != 1 ||
	    !input_event.pressed ||
	    sword3_input_poll(input, &input_event) != 1 ||
	    input_event.pressed)
		goto done;

	audio = sword3_audio_bridge_open(&config);
	if (!audio)
		goto done;
	wav = make_wav(&wav_size);
	if (!wav)
		goto done;
	effect = sword3_audio_open_memory(audio, SWORD3_AUDIO_EFFECT,
					 wav, wav_size);
	free(wav);
	wav = NULL;
	if (!effect || sword3_audio_play(effect, 0) < 0 ||
	    sword3_audio_play(effect, 0) < 0)
		goto done;
	sword3_audio_stop(effect);
	sword3_audio_close(effect);
	effect = NULL;

	wav = make_wav(&wav_size);
	if (!wav)
		goto done;
	music = sword3_audio_open_memory(audio, SWORD3_AUDIO_MUSIC,
					wav, wav_size);
	free(wav);
	wav = NULL;
	if (!music || sword3_audio_play(music, 0) < 0)
		goto done;
	sword3_audio_close(music);
	music = NULL;
	rc = 0;

done:
	if (rc < 0)
		fprintf(stderr, "SDL service test failed: %s\n", SDL_GetError());
	free(wav);
	sword3_audio_close(music);
	sword3_audio_close(effect);
	sword3_audio_bridge_close(audio);
	sword3_input_destroy(input);
	SDL_Quit();
	return rc;
}

int main(int argc, char **argv)
{
	Sword3Video *video = NULL;
	Sword3Video *skipped = NULL;
	Sword3VideoFrame frame;
	double previous_pts = -1.0;
	long expected_frames;
	int frame_count = 0;
	int rc = 1;

	if (argc != 3) {
		fprintf(stderr, "usage: %s short.mp4 expected-frame-count\n",
			argv[0]);
		return 2;
	}
	expected_frames = strtol(argv[2], NULL, 10);
	if (expected_frames <= 0)
		return 2;
	CHECK(test_sdl_services() == 0);

	video = sword3_video_open(argv[1]);
	CHECK(video != NULL);
	CHECK(sword3_video_status(video) == SWORD3_VIDEO_PLAYING);
	for (;;) {
		int decoded = sword3_video_next_frame(video, &frame);

		CHECK(decoded >= 0);
		if (!decoded)
			break;
		CHECK(frame.rgba != NULL);
		CHECK(frame.width > 0 && frame.height > 0);
		CHECK(frame.stride >= frame.width * 4);
		CHECK(isfinite(frame.pts_seconds));
		CHECK(isfinite(frame.duration_seconds));
		CHECK(frame.pts_seconds >= 0.0);
		CHECK(frame.duration_seconds >= 0.0);
		CHECK(previous_pts < 0.0 ||
		      frame.pts_seconds + 0.000001 >= previous_pts);
		previous_pts = frame.pts_seconds;
		frame_count++;
		CHECK(frame_count < 100000);
	}
	CHECK(frame_count > 0);
	CHECK(frame_count == expected_frames);
	CHECK(sword3_video_status(video) == SWORD3_VIDEO_COMPLETE);
	CHECK(sword3_video_next_frame(video, &frame) == 0);
	CHECK(frame.rgba == NULL && frame.width == 0 && frame.height == 0);

	skipped = sword3_video_open(argv[1]);
	CHECK(skipped != NULL);
	sword3_video_skip(skipped);
	CHECK(sword3_video_status(skipped) == SWORD3_VIDEO_SKIPPED);
	CHECK(sword3_video_next_frame(skipped, &frame) == 0);
	CHECK(frame.rgba == NULL);

	printf("decoded %d RGBA frames, final PTS %.6f seconds\n",
	       frame_count, previous_pts);
	rc = 0;

fail:
	if (video && sword3_video_status(video) == SWORD3_VIDEO_ERROR)
		fprintf(stderr, "decoder error: %s\n", sword3_video_error(video));
	sword3_video_close(skipped);
	sword3_video_close(video);
	return rc;
}
