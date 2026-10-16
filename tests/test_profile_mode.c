#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "peer_connection.h"

static const char audio[] =
    "m=audio 9 UDP/TLS/RTP/SAVPF 0\r\na=mid:0\r\na=sendrecv\r\na=rtcp-mux\r\n"
    "a=rtpmap:0 PCMU/8000\r\na=ssrc:11 cname:test\r\n";
static const char video[] =
    "m=video 9 UDP/TLS/RTP/SAVPF 125\r\na=mid:1\r\na=sendrecv\r\na=rtcp-mux\r\n"
    "a=rtpmap:125 H264/90000\r\na=fmtp:125 profile-level-id=42e01f;packetization-mode=1\r\n"
    "a=rtcp-fb:125 nack pli\r\na=ssrc:12 cname:test\r\n";

static PeerConfiguration profile(void) {
  PeerConfiguration config = {0};
  config.audio_codec = CODEC_PCMU;
  config.video_codec = CODEC_H264;
  return config;
}

static void check_rejected_in_strict(const char* offer, PeerConfiguration config) {
  PeerConnection* pc = peer_connection_create(&config);
  assert(pc);
  peer_connection_set_remote_description(pc, offer, SDP_TYPE_OFFER);
#if CONFIG_STRICT_CHROME_PROFILE
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_FAILED);
  assert(peer_connection_create_answer(pc) == NULL);
#else
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
#endif
  peer_connection_destroy(pc);
}

int main(void) {
  char offer[2048];
  PeerConfiguration config = profile();
  snprintf(offer, sizeof(offer), "v=0\r\na=group:BUNDLE 0 1\r\n%s%s", audio, video);
  PeerConnection* pc = peer_connection_create(&config);
  assert(pc);
  peer_connection_set_remote_description(pc, offer, SDP_TYPE_OFFER);
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
  const char* answer = peer_connection_create_answer(pc);
  assert(answer);
#if CONFIG_STRICT_CHROME_PROFILE
  assert(strstr(answer, "a=group:BUNDLE 0 1\r\n"));
  assert(strstr(answer, "m=audio") < strstr(answer, "m=video"));
  assert(strstr(answer, "m=video 9 UDP/TLS/RTP/SAVPF 125\r\n"));
#else
  assert(strstr(answer, "a=group:BUNDLE 1 2\r\n"));
  assert(strstr(answer, "m=video") < strstr(answer, "m=audio"));
  assert(strstr(answer, "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"));
#endif
  peer_connection_destroy(pc);
  const char* direction[] = {"a=recvonly\r\n", "a=sendonly\r\n", "a=inactive\r\n"};
  for (size_t i = 0; i < sizeof(direction) / sizeof(*direction); i++) {
    snprintf(offer, sizeof(offer), "v=0\r\na=group:BUNDLE 0 1\r\n%s%s%s", audio, video, direction[i]);
    check_rejected_in_strict(offer, config);
  }
  snprintf(offer, sizeof(offer), "v=0\r\na=group:BUNDLE 0\r\n%s", audio);
  check_rejected_in_strict(offer, config);
  snprintf(offer, sizeof(offer), "v=0\r\na=group:BUNDLE 1\r\n%s", video);
  check_rejected_in_strict(offer, config);
  snprintf(offer, sizeof(offer), "v=0\r\na=group:BUNDLE 0 1\r\n%s%s", audio, video);
  char* pli = strstr(offer, "nack pli");
  memcpy(pli, "nack xyz", 8);
  check_rejected_in_strict(offer, config);
  memcpy(pli, "nack pli", 8);
  config.video_codec = CODEC_NONE;
  check_rejected_in_strict(offer, config);
  config = profile(); config.audio_codec = CODEC_NONE;
  check_rejected_in_strict(offer, config);
  config = profile(); config.datachannel = DATA_CHANNEL_STRING;
  check_rejected_in_strict(offer, config);
#if CONFIG_STRICT_CHROME_PROFILE
  puts("Strict Chrome profile dispatch/failure regression: PASS");
#else
  puts("Generic upstream SDP behavior preservation regression: PASS");
#endif
  return 0;
}
