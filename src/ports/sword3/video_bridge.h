#ifndef SWORD3_VIDEO_BRIDGE_H
#define SWORD3_VIDEO_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sword3Video Sword3Video;

typedef enum Sword3VideoStatus {
	SWORD3_VIDEO_PLAYING = 0,
	SWORD3_VIDEO_COMPLETE = 1,
	SWORD3_VIDEO_SKIPPED = 2,
	SWORD3_VIDEO_ERROR = 3
} Sword3VideoStatus;

typedef struct Sword3VideoFrame {
	const uint8_t *rgba;
	int width;
	int height;
	int stride;
	double pts_seconds;
	double duration_seconds;
} Sword3VideoFrame;

/*
 * Opens a file through libavformat and selects its best video stream.
 * No SDL window, renderer, or texture is created. A Sword3Video is a
 * single-decoder state machine and must not be used concurrently.
 */
Sword3Video *sword3_video_open(const char *path);

/*
 * Decode the next RGBA frame. The pixel pointer remains valid until the next
 * sword3_video_next_frame call or close. Returns 1 for a frame, 0 at
 * completion/after skip, and -1 on error. PTS is normalized to the beginning
 * of the selected video stream, synthesized when missing, and never decreases.
 */
int sword3_video_next_frame(Sword3Video *video, Sword3VideoFrame *frame);

void sword3_video_skip(Sword3Video *video);
Sword3VideoStatus sword3_video_status(const Sword3Video *video);
const char *sword3_video_error(const Sword3Video *video);
void sword3_video_close(Sword3Video *video);

#ifdef __cplusplus
}
#endif

#endif
