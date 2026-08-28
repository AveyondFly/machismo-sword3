#include "video_bridge.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>

struct Sword3Video {
	AVFormatContext *format;
	AVCodecContext *codec;
	AVFrame *decoded;
	AVPacket *packet;
	struct SwsContext *scaler;
	AVStream *stream;
	uint8_t *rgba_data[4];
	int rgba_linesize[4];
	int rgba_width;
	int rgba_height;
	enum AVPixelFormat rgba_source_format;
	int stream_index;
	int packet_pending;
	int demux_eof;
	int drain_sent;
	int64_t first_pts;
	double last_pts_seconds;
	double next_pts_seconds;
	int have_output_pts;
	Sword3VideoStatus status;
	char error[AV_ERROR_MAX_STRING_SIZE + 96];
};

static void set_error(Sword3Video *video, int error, const char *operation)
{
	char av_error[AV_ERROR_MAX_STRING_SIZE];

	if (error < 0)
		av_strerror(error, av_error, sizeof(av_error));
	else
		snprintf(av_error, sizeof(av_error), "%s", "unknown error");
	snprintf(video->error, sizeof(video->error), "%s: %s",
		 operation, av_error);
	video->status = SWORD3_VIDEO_ERROR;
}

static int prepare_rgba(Sword3Video *video)
{
	int size;
	int rc;

	if (video->rgba_data[0] &&
	    video->rgba_width == video->decoded->width &&
	    video->rgba_height == video->decoded->height &&
	    video->rgba_source_format == video->decoded->format)
		return 0;
	rc = av_image_check_size((unsigned int)video->decoded->width,
				 (unsigned int)video->decoded->height, 0, NULL);
	if (rc < 0 || video->decoded->format < 0) {
		set_error(video, rc < 0 ? rc : AVERROR_INVALIDDATA,
			  "invalid decoded video frame");
		return -1;
	}

	video->scaler = sws_getCachedContext(
		video->scaler,
		video->decoded->width, video->decoded->height,
		(enum AVPixelFormat)video->decoded->format,
		video->decoded->width, video->decoded->height, AV_PIX_FMT_RGBA,
		SWS_BILINEAR, NULL, NULL, NULL);
	if (!video->scaler) {
		snprintf(video->error, sizeof(video->error),
			 "could not create RGBA conversion context");
		video->status = SWORD3_VIDEO_ERROR;
		return -1;
	}

	av_freep(&video->rgba_data[0]);
	size = av_image_alloc(video->rgba_data, video->rgba_linesize,
			      video->decoded->width, video->decoded->height,
			      AV_PIX_FMT_RGBA, 1);
	if (size < 0) {
		set_error(video, size, "could not allocate RGBA frame");
		return -1;
	}
	video->rgba_width = video->decoded->width;
	video->rgba_height = video->decoded->height;
	video->rgba_source_format =
		(enum AVPixelFormat)video->decoded->format;
	return 0;
}

Sword3Video *sword3_video_open(const char *path)
{
	Sword3Video *video;
	const AVCodec *decoder = NULL;
	int stream_index;
	int rc;

	if (!path) {
		errno = EINVAL;
		return NULL;
	}
	video = calloc(1, sizeof(*video));
	if (!video) {
		errno = ENOMEM;
		return NULL;
	}
	video->stream_index = -1;
	video->first_pts = AV_NOPTS_VALUE;
	video->rgba_source_format = AV_PIX_FMT_NONE;
	video->status = SWORD3_VIDEO_PLAYING;

	rc = avformat_open_input(&video->format, path, NULL, NULL);
	if (rc < 0) {
		set_error(video, rc, "could not open video");
		goto fail;
	}
	rc = avformat_find_stream_info(video->format, NULL);
	if (rc < 0) {
		set_error(video, rc, "could not read stream information");
		goto fail;
	}
	stream_index = av_find_best_stream(video->format, AVMEDIA_TYPE_VIDEO,
					   -1, -1, &decoder, 0);
	if (stream_index < 0) {
		set_error(video, stream_index, "could not find video stream");
		goto fail;
	}
	video->stream_index = stream_index;
	video->stream = video->format->streams[stream_index];
	video->codec = avcodec_alloc_context3(decoder);
	if (!video->codec) {
		snprintf(video->error, sizeof(video->error),
			 "could not allocate video decoder");
		video->status = SWORD3_VIDEO_ERROR;
		goto fail;
	}
	rc = avcodec_parameters_to_context(video->codec,
					   video->stream->codecpar);
	if (rc < 0) {
		set_error(video, rc, "could not configure video decoder");
		goto fail;
	}
	video->codec->pkt_timebase = video->stream->time_base;
	rc = avcodec_open2(video->codec, decoder, NULL);
	if (rc < 0) {
		set_error(video, rc, "could not open video decoder");
		goto fail;
	}
	video->decoded = av_frame_alloc();
	video->packet = av_packet_alloc();
	if (!video->decoded || !video->packet) {
		snprintf(video->error, sizeof(video->error),
			 "could not allocate decode buffers");
		video->status = SWORD3_VIDEO_ERROR;
		goto fail;
	}
	return video;

fail:
	sword3_video_close(video);
	errno = EIO;
	return NULL;
}

static double frame_duration_seconds(Sword3Video *video)
{
	if (video->decoded->duration > 0)
		return (double)video->decoded->duration *
		       av_q2d(video->stream->time_base);
	if (video->stream->avg_frame_rate.num > 0 &&
	    video->stream->avg_frame_rate.den > 0)
		return av_q2d(av_inv_q(video->stream->avg_frame_rate));
	return 0.0;
}

static double frame_pts_seconds(Sword3Video *video, double duration)
{
	int64_t timestamp = video->decoded->best_effort_timestamp;
	double pts;

	if (timestamp == AV_NOPTS_VALUE) {
		pts = video->have_output_pts ? video->next_pts_seconds : 0.0;
	} else {
		int64_t origin;

		if (video->stream->start_time != AV_NOPTS_VALUE) {
			origin = video->stream->start_time;
		} else {
			if (video->first_pts == AV_NOPTS_VALUE)
				video->first_pts = timestamp;
			origin = video->first_pts;
		}
		pts = (double)(timestamp - origin) *
		      av_q2d(video->stream->time_base);
	}
	/*
	 * best_effort_timestamp is normally monotonic in decoder output order.
	 * Missing or malformed timestamps must not move playback backwards.
	 */
	if (video->have_output_pts && pts < video->last_pts_seconds)
		pts = video->last_pts_seconds;
	if (!video->have_output_pts && pts < 0.0)
		pts = 0.0;
	video->last_pts_seconds = pts;
	video->next_pts_seconds = pts + (duration > 0.0 ? duration : 0.0);
	video->have_output_pts = 1;
	return pts;
}

static int output_frame(Sword3Video *video, Sword3VideoFrame *frame)
{
	double duration;
	int rows;

	if (prepare_rgba(video) < 0)
		return -1;
	rows = sws_scale(video->scaler,
			 (const uint8_t *const *)video->decoded->data,
			 video->decoded->linesize, 0, video->decoded->height,
			 video->rgba_data, video->rgba_linesize);
	if (rows != video->decoded->height) {
		snprintf(video->error, sizeof(video->error),
			 "RGBA conversion returned %d of %d rows",
			 rows, video->decoded->height);
		video->status = SWORD3_VIDEO_ERROR;
		return -1;
	}
	frame->rgba = video->rgba_data[0];
	frame->width = video->decoded->width;
	frame->height = video->decoded->height;
	frame->stride = video->rgba_linesize[0];
	duration = frame_duration_seconds(video);
	frame->pts_seconds = frame_pts_seconds(video, duration);
	frame->duration_seconds = duration;
	av_frame_unref(video->decoded);
	return 1;
}

int sword3_video_next_frame(Sword3Video *video, Sword3VideoFrame *frame)
{
	int rc;

	if (!video || !frame) {
		errno = EINVAL;
		return -1;
	}
	*frame = (Sword3VideoFrame){0};
	if (video->status != SWORD3_VIDEO_PLAYING)
		return video->status == SWORD3_VIDEO_ERROR ? -1 : 0;

	for (;;) {
		rc = avcodec_receive_frame(video->codec, video->decoded);
		if (rc == 0)
			return output_frame(video, frame);
		if (rc == AVERROR_EOF) {
			video->status = SWORD3_VIDEO_COMPLETE;
			return 0;
		}
		if (rc != AVERROR(EAGAIN)) {
			set_error(video, rc, "video decode failed");
			return -1;
		}

		if (video->packet_pending) {
			rc = avcodec_send_packet(video->codec, video->packet);
			if (rc == 0) {
				video->packet_pending = 0;
				av_packet_unref(video->packet);
				continue;
			}
			if (rc == AVERROR(EAGAIN)) {
				set_error(video, AVERROR_BUG,
					  "decoder rejected input while needing input");
				return -1;
			}
			set_error(video, rc, "could not submit video packet");
			return -1;
		}

		if (video->demux_eof) {
			if (!video->drain_sent) {
				rc = avcodec_send_packet(video->codec, NULL);
				if (rc == 0) {
					video->drain_sent = 1;
					continue;
				}
				if (rc == AVERROR_EOF) {
					video->status = SWORD3_VIDEO_COMPLETE;
					return 0;
				}
				if (rc == AVERROR(EAGAIN)) {
					set_error(video, AVERROR_BUG,
						  "decoder could not accept drain");
					return -1;
				}
				if (rc < 0) {
					set_error(video, rc,
						  "could not drain video decoder");
					return -1;
				}
			}
			set_error(video, AVERROR_BUG,
				  "decoder requested input after drain");
			return -1;
		}

		rc = av_read_frame(video->format, video->packet);
		if (rc == AVERROR_EOF) {
			video->demux_eof = 1;
			continue;
		}
		if (rc < 0) {
			set_error(video, rc, "could not read video packet");
			return -1;
		}
		if (video->packet->stream_index != video->stream_index) {
			av_packet_unref(video->packet);
			continue;
		}
		video->packet_pending = 1;
	}
}

void sword3_video_skip(Sword3Video *video)
{
	if (video && video->status == SWORD3_VIDEO_PLAYING)
		video->status = SWORD3_VIDEO_SKIPPED;
}

Sword3VideoStatus sword3_video_status(const Sword3Video *video)
{
	return video ? video->status : SWORD3_VIDEO_ERROR;
}

const char *sword3_video_error(const Sword3Video *video)
{
	return video ? video->error : "invalid Sword3 video handle";
}

void sword3_video_close(Sword3Video *video)
{
	if (!video)
		return;
	av_freep(&video->rgba_data[0]);
	sws_freeContext(video->scaler);
	av_packet_free(&video->packet);
	av_frame_free(&video->decoded);
	avcodec_free_context(&video->codec);
	avformat_close_input(&video->format);
	free(video);
}
