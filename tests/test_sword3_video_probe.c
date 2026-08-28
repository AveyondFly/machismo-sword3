#include "ports/sword3/video_bridge.h"

#include <stdio.h>

int main(int argc, char **argv)
{
	Sword3Video *video;
	Sword3VideoFrame frame;
	double previous_pts = -1.0;
	unsigned long frames = 0;
	int rc;

	if (argc != 2) {
		fprintf(stderr, "usage: %s VIDEO.mp4\n", argv[0]);
		return 2;
	}
	video = sword3_video_open(argv[1]);
	if (!video) {
		fprintf(stderr, "could not open %s\n", argv[1]);
		return 1;
	}
	while ((rc = sword3_video_next_frame(video, &frame)) > 0) {
		if (!frame.rgba || frame.width <= 0 || frame.height <= 0 ||
		    frame.stride < frame.width * 4 ||
		    frame.pts_seconds + 0.001 < previous_pts) {
			fprintf(stderr, "invalid frame %lu (pts %.6f)\n",
				frames, frame.pts_seconds);
			sword3_video_close(video);
			return 1;
		}
		previous_pts = frame.pts_seconds;
		frames++;
	}
	if (rc < 0) {
		fprintf(stderr, "decode failed: %s\n", sword3_video_error(video));
		sword3_video_close(video);
		return 1;
	}
	if (!frames || sword3_video_status(video) != SWORD3_VIDEO_COMPLETE) {
		fprintf(stderr, "video did not decode to completion\n");
		sword3_video_close(video);
		return 1;
	}
	printf("decoded %lu RGBA frames, final pts %.3f seconds\n",
	       frames, previous_pts);
	sword3_video_close(video);
	return 0;
}
