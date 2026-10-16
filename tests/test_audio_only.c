#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "peer_connection.h"
#include "sdp.h"

#if !CONFIG_STRICT_CHROME_PROFILE
int main(void) {
  puts("Strict PCMU-only dispatch regression: skipped in generic build");
  return 0;
}
#else

static const char audio[] =
    "m=audio 9 UDP/TLS/RTP/SAVPF 0\r\n"
    "a=mid:2\r\na=sendrecv\r\na=rtcp-mux\r\n"
    "a=rtpmap:0 PCMU/8000\r\n";

static PeerConfiguration profile(void) {
  PeerConfiguration config = {0};
  config.video_codec = CODEC_NONE;
  config.audio_codec = CODEC_PCMU;
  return config;
}

static void replace(char* text, size_t size, const char* old, const char* value) {
  char* pos = strstr(text, old);
  assert(pos);
  size_t before = pos - text, tail = strlen(pos + strlen(old));
  assert(before + strlen(value) + tail < size);
  memmove(pos + strlen(value), pos + strlen(old), tail + 1);
  memcpy(pos, value, strlen(value));
}

static void description(char* sdp, size_t size, const char* setup) {
  int n = snprintf(sdp, size,
      "v=0\r\na=group:BUNDLE 2\r\na=setup:%s\r\n"
      "a=ice-ufrag:test\r\na=ice-pwd:abcdefghijklmnopqrstuvwx\r\n"
      "a=fingerprint:sha-256 "
      "01:02:03:04:05:06:07:08:09:0A:0B:0C:0D:0E:0F:10:"
      "11:12:13:14:15:16:17:18:19:1A:1B:1C:1D:1E:1F:20\r\n%s", setup, audio);
  assert(n > 0 && (size_t)n < size);
}

static PeerConnection* create(int offerer) {
  PeerConfiguration config = profile();
  PeerConnection* pc = peer_connection_create(&config);
  assert(pc);
  if (offerer) assert(peer_connection_create_offer(pc));
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_NEW);
  return pc;
}

static void one_audio(const char* sdp, const char* mid) {
  const char* line = strstr(sdp, "m=audio 9 UDP/TLS/RTP/SAVPF 0\r\n");
  assert(line && !strstr(line + 2, "\r\nm="));
  assert(!strstr(sdp, "m=video") && !strstr(sdp, "m=application"));
  char bundle[80];
  snprintf(bundle, sizeof(bundle), "a=group:BUNDLE %s\r\n", mid);
  assert(strstr(sdp, bundle));
  SdpMedia media[2];
  assert(sdp_parse_media(sdp, media, CODEC_NONE, CODEC_PCMU) == 1);
  assert(!media[0].video && media[0].payload_type == 0 && !strcmp(media[0].mid, mid));
  assert(!media[1].mid[0]);
}

static void test_native_offer_answer(void) {
  PeerConnection* offerer = create(1);
  char offer[CONFIG_SDP_BUFFER_SIZE];
  strcpy(offer, peer_connection_create_offer(offerer));
  one_audio(offer, "2");
  assert(strstr(offer, "a=setup:actpass\r\n"));
  assert(strstr(offer, "a=candidate:"));
  assert(!strstr(offer, "H264") && !strstr(offer, "opus"));
  PeerConnection* answerer = create(0);
  peer_connection_set_remote_description(answerer, offer, SDP_TYPE_OFFER);
  assert(peer_connection_get_state(answerer) == PEER_CONNECTION_CHECKING);
  const char* answer = peer_connection_create_answer(answerer);
  assert(answer);
  one_audio(answer, "2");
  assert(strstr(answer, "a=setup:active\r\n") && !strstr(answer, "a=setup:actpass"));
  SdpMedia media[2];
  assert(sdp_parse_answer(answer, media, CODEC_NONE, CODEC_PCMU) == 1);
  peer_connection_set_remote_description(offerer, answer, SDP_TYPE_ANSWER);
  assert(peer_connection_get_state(offerer) == PEER_CONNECTION_CHECKING);
  peer_connection_destroy(answerer);
  peer_connection_destroy(offerer);
}

static void test_browser_offer_answer(void) {
  const char* mids[] = {"0", "audio"};
  for (size_t i = 0; i < sizeof(mids) / sizeof(*mids); i++) {
    char offer[CONFIG_SDP_BUFFER_SIZE];
    description(offer, sizeof(offer), "actpass");
    replace(offer, sizeof(offer), "BUNDLE 2", i ? "BUNDLE audio" : "BUNDLE 0");
    replace(offer, sizeof(offer), "a=mid:2", i ? "a=mid:audio" : "a=mid:0");
    /* Browser codec alternatives are offered, but the answer selects only PT0.
     * A missing SSRC must not disable the negotiated audio track. */
    replace(offer, sizeof(offer), "SAVPF 0", "SAVPF 111 0 8");
    replace(offer, sizeof(offer), "PCMU/8000", "PCMU/8000/1");
    strcat(offer, "a=rtpmap:111 opus/48000/2\r\na=rtpmap:8 PCMA/8000\r\n");
    PeerConnection* pc = create(0);
    peer_connection_set_remote_description(pc, offer, SDP_TYPE_OFFER);
    assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
    const char* answer = peer_connection_create_answer(pc);
    assert(answer);
    one_audio(answer, mids[i]);
    assert(!strstr(answer, "opus") && !strstr(answer, "PCMA"));
    assert(strstr(answer, "a=setup:active\r\n"));
    peer_connection_destroy(pc);
  }
}

static void test_answer_validation(void) {
  const char* roles[] = {"active", "passive"};
  for (size_t i = 0; i < sizeof(roles) / sizeof(*roles); i++) {
    char answer[CONFIG_SDP_BUFFER_SIZE];
    description(answer, sizeof(answer), roles[i]);
    replace(answer, sizeof(answer), "PCMU/8000", "PCMU/8000/1");
    SdpMedia media[2];
    assert(sdp_parse_answer(answer, media, CODEC_NONE, CODEC_PCMU) == 1);
    PeerConnection* pc = create(1);
    peer_connection_set_remote_description(pc, answer, SDP_TYPE_ANSWER);
    assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
    peer_connection_set_remote_description(pc, answer, SDP_TYPE_ANSWER);
    assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
    peer_connection_destroy(pc);
  }
  /* The one media-level DTLS setup may override the session role. */
  char answer[CONFIG_SDP_BUFFER_SIZE];
  description(answer, sizeof(answer), "passive");
  strcat(answer, "a=setup:active\r\n");
  PeerConnection* pc = create(1);
  peer_connection_set_remote_description(pc, answer, SDP_TYPE_ANSWER);
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
  peer_connection_destroy(pc);
}

static void rejected(const char* sdp, SdpType type) {
  SdpMedia media[2];
  int result = type == SDP_TYPE_ANSWER ?
      sdp_parse_answer(sdp, media, CODEC_NONE, CODEC_PCMU) :
      sdp_parse_media(sdp, media, CODEC_NONE, CODEC_PCMU);
  assert(result < 0);
  PeerConnection* pc = create(type == SDP_TYPE_ANSWER);
  peer_connection_set_remote_description(pc, sdp, type);
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_FAILED);
  assert(peer_connection_create_answer(pc) == NULL);
  assert(peer_connection_create_offer(pc) == NULL);
  peer_connection_destroy(pc);
}

static void test_rejection_mutations(void) {
  const char* changes[][2] = {
      {"BUNDLE 2", "BUNDLE 1"},
      {"BUNDLE 2", "BUNDLE 2 1"},
      {"BUNDLE 2", "BUNDLE 2 2"},
      {"a=group:BUNDLE 2\r\n", ""},
      {"a=group:BUNDLE 2\r\n", "a=group:BUNDLE 2\r\na=group:BUNDLE 2\r\n"},
      {"a=mid:2", "a=mid:1"},
      {"a=mid:2\r\n", ""},
      {"a=mid:2", "a=mid:2\r\na=mid:2"},
      {"a=mid:2", "a=mid:2 1"},
      {"m=audio 9", "m=audio 0"},
      {"m=audio 9", "m=video 9"},
      {"m=audio 9", "m=application 9"},
      {"UDP/TLS/RTP/SAVPF", "UDP/TLS/RTP/SAVP"},
      {"SAVPF 0", "SAVPF 8"},
      {"SAVPF 0", "SAVPF 128"},
      {"PCMU/8000", "PCMA/8000"},
      {"PCMU/8000", "PCMU/16000"},
      {"PCMU/8000", "PCMU/8000/2"},
      {"PCMU/8000", "PCMU/8000 garbage"},
      {"a=rtpmap:0 PCMU/8000\r\n", "a=rtpmap:0 PCMU/8000\r\na=rtpmap:128 PCMA/8000\r\n"},
      {"a=rtpmap:0 PCMU/8000\r\n", "a=rtpmap:0 PCMU/8000\r\na=rtpmap:8 PCMA/8000\r\n"},
      {"a=rtpmap:0 PCMU/8000\r\n", "a=rtpmap:0 PCMU/8000\r\na=rtpmap:-1 PCMA/8000\r\n"},
      {"a=rtpmap:0 PCMU/8000\r\n", "a=rtpmap:0 PCMU/8000\r\na=rtpmap:garbage\r\n"},
      {"a=rtpmap:0 PCMU/8000\r\n", ""},
      {"a=rtpmap:0", "a=rtpmap:8"},
      {"a=rtcp-mux\r\n", ""},
      {"a=sendrecv", "a=sendonly"},
      {"a=sendrecv", "a=recvonly"},
      {"a=sendrecv", "a=inactive"},
  };
  for (int answer = 0; answer < 2; answer++) {
    SdpType type = answer ? SDP_TYPE_ANSWER : SDP_TYPE_OFFER;
    for (size_t i = 0; i < sizeof(changes) / sizeof(*changes); i++) {
      char sdp[CONFIG_SDP_BUFFER_SIZE];
      description(sdp, sizeof(sdp), answer ? "active" : "actpass");
      replace(sdp, sizeof(sdp), changes[i][0], changes[i][1]);
      rejected(sdp, type);
    }
    const char* extra[] = {audio,
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=mid:1\r\n",
        "m=video 0 UDP/TLS/RTP/SAVPF 96\r\na=mid:1\r\n",
        "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"};
    for (size_t i = 0; i < sizeof(extra) / sizeof(*extra); i++) {
      char sdp[CONFIG_SDP_BUFFER_SIZE];
      description(sdp, sizeof(sdp), answer ? "active" : "actpass");
      strcat(sdp, extra[i]);
      rejected(sdp, type);
    }
  }
  const char* answer_changes[][2] = {
      {"SAVPF 0", "SAVPF 0 8"},
      {"SAVPF 0", "SAVPF 0 0"},
      {"PCMU/8000", "PCMU/8000 garbage"},
      {"a=rtpmap:0 PCMU/8000\r\n", "a=rtpmap:0 PCMU/8000\r\na=rtpmap:8 PCMA/8000\r\n"},
      {"a=rtpmap:0 PCMU/8000\r\n", "a=rtpmap:0 PCMU/8000\r\na=rtpmap:0 PCMU/8000\r\n"},
      {"a=rtpmap:0 PCMU/8000\r\n", "a=rtpmap:0 PCMU/8000\r\na=fmtp:8 invalid\r\n"},
  };
  for (size_t i = 0; i < sizeof(answer_changes) / sizeof(*answer_changes); i++) {
    char sdp[CONFIG_SDP_BUFFER_SIZE];
    description(sdp, sizeof(sdp), "active");
    replace(sdp, sizeof(sdp), answer_changes[i][0], answer_changes[i][1]);
    rejected(sdp, SDP_TYPE_ANSWER);
  }
}

static void test_profile_mismatch(void) {
  char sdp[CONFIG_SDP_BUFFER_SIZE];
  description(sdp, sizeof(sdp), "actpass");
  SdpMedia media[2];
  assert(sdp_parse_media(sdp, media, CODEC_H264, CODEC_PCMU) < 0);
  assert(sdp_parse_media(sdp, media, CODEC_NONE, CODEC_NONE) < 0);
  assert(sdp_parse_media(sdp, media, CODEC_VP8, CODEC_PCMU) < 0);
  PeerConfiguration config = profile();
  config.video_codec = CODEC_H264;
  PeerConnection* pc = peer_connection_create(&config);
  assert(pc);
  peer_connection_set_remote_description(pc, sdp, SDP_TYPE_OFFER);
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_FAILED);
  peer_connection_destroy(pc);
  config = profile();
  config.datachannel = DATA_CHANNEL_STRING;
  pc = peer_connection_create(&config);
  assert(pc);
  peer_connection_set_remote_description(pc, sdp, SDP_TYPE_OFFER);
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_FAILED);
  peer_connection_destroy(pc);
}

int main(void) {
  test_native_offer_answer();
  test_browser_offer_answer();
  test_answer_validation();
  test_rejection_mutations();
  test_profile_mismatch();
  puts("Strict PCMU-only offer/answer regression: PASS");
  return 0;
}
#endif
