#ifndef SWORD3_FFMPEG_COMPAT_H
#define SWORD3_FFMPEG_COMPAT_H

/*
 * FFmpeg 5.1 (libavutil 57.23.100) replaced the plain int channels member of
 * AVFrame/AVCodecContext with an AVChannelLayout ch_layout, and 6.0 removed
 * the old field entirely; 5.1 likewise renamed AVFrame's pkt_duration to
 * duration. Toolchains for this port span both generations (the Docker image
 * ships FFmpeg 4.2, newer cross toolchains ship 5.1+), so read those fields
 * through these shims instead of a fixed member.
 */
#include <libavutil/version.h>

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 23, 100)
#define SWORD3_AV_CHANNELS(obj) ((obj)->ch_layout.nb_channels)
#else
#define SWORD3_AV_CHANNELS(obj) ((obj)->channels)
#endif

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
#define SWORD3_AV_FRAME_DURATION(frame) ((frame)->duration)
#else
#define SWORD3_AV_FRAME_DURATION(frame) ((frame)->pkt_duration)
#endif

#endif
