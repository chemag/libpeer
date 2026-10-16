#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdp.h"

static const char audio[] =
    "m=audio 9 UDP/TLS/RTP/SAVPF 111 0 8\r\n"
    "a=mid:0\r\na=sendrecv\r\na=rtcp-mux\r\na=rtpmap:0 PCMU/8000\r\n";
static const char video[] =
    "m=video 9 UDP/TLS/RTP/SAVPF 96 102 125 126\r\n"
    "a=mid:1\r\na=sendrecv\r\na=rtcp-mux\r\n"
    "a=rtpmap:96 VP8/90000\r\na=rtpmap:102 H264/90000\r\n"
    "a=fmtp:102 packetization-mode=1;profile-level-id=42001f\r\n"
    "a=rtpmap:125 H264/90000\r\na=rtcp-fb:125 nack pli\r\n"
    "a=fmtp:125 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
    "a=rtpmap:126 rtx/90000\r\na=fmtp:126 apt=125\r\n";

int main(void) {
  char offer[CONFIG_SDP_BUFFER_SIZE], answer[CONFIG_SDP_BUFFER_SIZE] = "";
  SdpMedia media[2];
  snprintf(offer, sizeof(offer), "v=0\r\na=group:BUNDLE 0 1\r\n%s%s", audio, video);
  assert(sdp_parse_media(offer, media, CODEC_H264, CODEC_PCMU) == 2);
  assert(!media[0].video && media[0].payload_type == 0 && !strcmp(media[0].mid, "0"));
  assert(media[1].video && media[1].payload_type == 125 && !strcmp(media[1].direction, "sendrecv"));
  sdp_append_media(answer, &media[0]);
  sdp_append_media(answer, &media[1]);
  assert(strstr(answer, "m=audio") < strstr(answer, "m=video"));
  assert(strstr(answer, "a=fmtp:125 packetization-mode=1;profile-level-id=42e01f"));
  assert(!strstr(answer, "a=rtpmap:96") && !strstr(answer, "a=rtpmap:126"));
  snprintf(offer, sizeof(offer), "v=0\r\na=group:BUNDLE 1 0\r\n%s%s", video, audio);
  assert(sdp_parse_media(offer, media, CODEC_H264, CODEC_PCMU) == 2 && media[0].video);
  char* profile = strstr(offer, "42e01f");
  memcpy(profile, "64001f", 6);
  assert(sdp_parse_media(offer, media, CODEC_H264, CODEC_PCMU) < 0);
  memcpy(profile, "42e01f", 6);
  char* mode = strstr(profile - 60, "packetization-mode=1");
  assert(mode);
  mode[strlen("packetization-mode=")] = '0';
  assert(sdp_parse_media(offer, media, CODEC_H264, CODEC_PCMU) < 0);
  snprintf(offer, sizeof(offer), "v=0\r\na=group:BUNDLE 0\r\n%s", audio);
  assert(sdp_parse_media(offer, media, CODEC_NONE, CODEC_PCMU) == 1);
  assert(!media[0].video && media[0].payload_type == 0 && !strcmp(media[0].mid, "0"));
  assert(sdp_parse_media(offer, media, CODEC_H264, CODEC_PCMU) < 0);
  assert(sdp_parse_media(offer, media, CODEC_H264, CODEC_NONE) < 0);
  snprintf(offer, sizeof(offer), "v=0\r\na=group:BUNDLE 0 1\r\n%s%s", audio, video);
  char* feedback = strstr(offer, "nack pli");
  memcpy(feedback, "nack xyz", 8);
  assert(sdp_parse_media(offer, media, CODEC_H264, CODEC_PCMU) < 0);
  memcpy(feedback, "nack pli", 8);
  const char* directions[] = {"a=sendonly\r\n", "a=recvonly\r\n", "a=inactive\r\n"};
  for (size_t i = 0; i < sizeof(directions) / sizeof(*directions); i++) {
    snprintf(offer, sizeof(offer), "v=0\r\na=group:BUNDLE 0 1\r\n%s%s%s", audio, video, directions[i]);
    assert(sdp_parse_media(offer, media, CODEC_H264, CODEC_PCMU) < 0);
  }
  assert(sdp_parse_media("m=audio", media, CODEC_NONE, CODEC_PCMU) < 0);
  assert(sdp_append(answer, "%s", "") == 0);
  memset(answer, 'x', sizeof(answer) - 1);
  answer[sizeof(answer) - 1] = '\0';
  assert(sdp_append(answer, "%s", "too long") < 0);
  puts("Chrome SDP profile regression: PASS");
  return 0;
}
