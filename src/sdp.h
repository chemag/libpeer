#ifndef SDP_H_
#define SDP_H_

#include <string.h>
#include "config.h"
#include "peer_connection.h"

/* Bounded, one-track-per-kind H264/PCMU browser-offerer profile. */
typedef struct SdpMedia {
  int video;
  int payload_type;
  char mid[32];
  char direction[12];
} SdpMedia;

int sdp_parse_media(const char* sdp, SdpMedia media[2], MediaCodec video, MediaCodec audio);
void sdp_append_media(char* sdp, const SdpMedia* media);

#define SDP_ATTR_LENGTH 128

#ifndef ICE_LITE
#define ICE_LITE 0
#endif

void sdp_append_h264(char* sdp);

void sdp_append_vp8(char* sdp);

void sdp_append_pcma(char* sdp);

void sdp_append_pcmu(char* sdp);

void sdp_append_opus(char* sdp);

void sdp_append_datachannel(char* sdp);

void sdp_create(char* sdp, int b_video, int b_audio, int b_datachannel);

int sdp_append(char* sdp, const char* format, ...);

void sdp_reset(char* sdp);

#endif  // SDP_H_
