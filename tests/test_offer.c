#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dtls_srtp.h"
#include "peer_connection.h"
#include "sdp.h"

static PeerConfiguration profile(void) {
  PeerConfiguration config = {0};
  config.video_codec = CODEC_H264;
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

static void make_answer(char* answer, size_t size, const char* setup) {
  int n = snprintf(answer, size,
      "v=0\r\na=group:BUNDLE 1 2\r\n%s"
      "a=ice-ufrag:test\r\na=ice-pwd:abcdefghijklmnopqrstuvwx\r\n"
      "a=fingerprint:sha-256 "
      "01:02:03:04:05:06:07:08:09:0A:0B:0C:0D:0E:0F:10:"
      "11:12:13:14:15:16:17:18:19:1A:1B:1C:1D:1E:1F:20\r\n"
      "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
      "a=mid:1\r\na=sendrecv\r\na=rtcp-mux\r\n"
      "a=rtpmap:96 H264/90000\r\n"
      "a=fmtp:96 packetization-mode=1;profile-level-id=42c01f;level-asymmetry-allowed=1\r\n"
      "a=rtcp-fb:96 nack pli\r\na=ssrc:21 cname:test\r\n"
      "m=audio 9 UDP/TLS/RTP/SAVPF 0\r\n"
      "a=mid:2\r\na=sendrecv\r\na=rtcp-mux\r\n"
      "a=rtpmap:0 PCMU/8000\r\na=ssrc:22 cname:test\r\n", setup);
  assert(n > 0 && (size_t)n < size);
}

static PeerConnection* offerer(void) {
  PeerConfiguration config = profile();
  PeerConnection* pc = peer_connection_create(&config);
  assert(pc);
  assert(peer_connection_create_offer(pc));
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_NEW);
  return pc;
}

static void check_rejected(const char* answer) {
  PeerConnection* pc = offerer();
  peer_connection_set_remote_description(pc, answer, SDP_TYPE_ANSWER);
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_FAILED);
  assert(peer_connection_create_offer(pc) == NULL);
  assert(peer_connection_create_answer(pc) == NULL);
  peer_connection_destroy(pc);
}

static void check_media_rejected(const char* answer) {
  SdpMedia media[2];
  assert(sdp_parse_answer(answer, media, CODEC_H264, CODEC_PCMU) < 0);
  PeerConnection* pc = offerer();
  peer_connection_set_remote_description(pc, answer, SDP_TYPE_ANSWER);
#if CONFIG_STRICT_CHROME_PROFILE
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_FAILED);
#else
  /* Answer safety does not impose the constrained codecs on generic dispatch. */
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
#endif
  peer_connection_destroy(pc);
}

static void test_offer(void) {
  PeerConnection* pc = offerer();
  const char* offer = peer_connection_create_offer(pc);
  assert(offer);
  assert(strstr(offer, "a=setup:actpass\r\n"));
  assert(!strstr(offer, "a=setup:passive") && !strstr(offer, "a=setup:active"));
  assert(strstr(offer, "a=group:BUNDLE 1 2\r\n"));
  assert(strstr(offer, "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"));
  assert(strstr(offer, "m=audio 9 UDP/TLS/RTP/SAVPF 0\r\n"));
  assert(strstr(offer, "a=fmtp:96 packetization-mode=1;profile-level-id=42c01f;"));
  assert(strstr(offer, "m=video") < strstr(offer, "m=audio"));
  const char* candidate = strstr(offer, "a=candidate:");
  assert(candidate && candidate > strstr(offer, "m=video") &&
         candidate < strstr(offer, "m=audio"));
  SdpMedia media[2];
  assert(sdp_parse_media(offer, media, CODEC_H264, CODEC_PCMU) == 2);
  assert(media[0].video && media[0].payload_type == 96 && !strcmp(media[0].mid, "1"));
  assert(!media[1].video && media[1].payload_type == 0 && !strcmp(media[1].mid, "2"));
  peer_connection_destroy(pc);

  /* Existing browser-offerer 42e01f mapping and active answer remain intact. */
  char browser_offer[CONFIG_SDP_BUFFER_SIZE];
  make_answer(browser_offer, sizeof(browser_offer), "a=setup:actpass\r\n");
  replace(browser_offer, sizeof(browser_offer), "42c01f", "42e01f");
  replace(browser_offer, sizeof(browser_offer), "SAVPF 96", "SAVPF 125");
  replace(browser_offer, sizeof(browser_offer), "a=rtpmap:96", "a=rtpmap:125");
  replace(browser_offer, sizeof(browser_offer), "a=fmtp:96", "a=fmtp:125");
  replace(browser_offer, sizeof(browser_offer), "a=rtcp-fb:96", "a=rtcp-fb:125");
  PeerConfiguration config = profile();
  pc = peer_connection_create(&config);
  assert(pc);
  peer_connection_set_remote_description(pc, browser_offer, SDP_TYPE_OFFER);
  const char* answer = peer_connection_create_answer(pc);
  assert(answer && strstr(answer, "a=setup:active\r\n"));
  assert(!strstr(answer, "a=setup:actpass"));
#if CONFIG_STRICT_CHROME_PROFILE
  assert(strstr(answer, "m=video 9 UDP/TLS/RTP/SAVPF 125\r\n"));
  assert(strstr(answer, "profile-level-id=42e01f"));
#else
  assert(strstr(answer, "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"));
#endif
  peer_connection_destroy(pc);
}

static void test_answer_media(void) {
  char answer[CONFIG_SDP_BUFFER_SIZE];
  SdpMedia media[2];
  const char* profiles[] = {"42c01f", "42e01f", "4d801f", "58c01f", "42C01F"};
  for (size_t i = 0; i < sizeof(profiles) / sizeof(*profiles); i++) {
    make_answer(answer, sizeof(answer), "a=setup:active\r\n");
    replace(answer, sizeof(answer), "42c01f", profiles[i]);
    assert(sdp_parse_answer(answer, media, CODEC_H264, CODEC_PCMU) == 2);
    assert(media[0].payload_type == 96 && media[1].payload_type == 0);
    PeerConnection* pc = offerer();
    peer_connection_set_remote_description(pc, answer, SDP_TYPE_ANSWER);
    assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
    peer_connection_destroy(pc);
  }
  const char* bad_profiles[] = {"42001f", "64001f", "42c020", "42c01e", "42c000", "42g01f"};
  for (size_t i = 0; i < sizeof(bad_profiles) / sizeof(*bad_profiles); i++) {
    make_answer(answer, sizeof(answer), "a=setup:active\r\n");
    replace(answer, sizeof(answer), "42c01f", bad_profiles[i]);
    check_media_rejected(answer);
  }
  const char* changes[][2] = {
      {"packetization-mode=1", "packetization-mode=0"},
      {"packetization-mode=1", "packetization-mode=10"},
      {"packetization-mode=1;", ""},
      {"packetization-mode=1", "packetization-mode=1;packetization-mode=0"},
      {"profile-level-id=42c01f", "profile-level-id=42c01f;profile-level-id=64001f"},
      {"SAVPF 96", "SAVPF 125"},
      {"SAVPF 96", "SAVPF 96 125"},
      {"SAVPF 96", "SAVPF 96 96"},
      {"H264/90000", "VP8/90000"},
      {"H264/90000", "H264/90000 garbage"},
      {"a=rtpmap:96 H264/90000\r\n", "a=rtpmap:96 H264/90000\r\na=rtpmap:96 VP8/90000\r\n"},
      {"SAVPF 0", "SAVPF 8"},
      {"PCMU/8000", "PCMA/8000"},
      {"UDP/TLS/RTP/SAVPF", "UDP/TLS/RTP/SAVP"},
      {"m=video 9", "m=video 0"},
      {"a=mid:1", "a=mid:video"},
      {"a=mid:2", "a=mid:1"},
      {"a=group:BUNDLE 1 2", "a=group:BUNDLE 2 1"},
      {"a=rtcp-mux\r\n", ""},
      {"a=sendrecv", "a=recvonly"},
      {"a=sendrecv", "a=sendonly"},
      {"a=sendrecv", "a=inactive"},
      {"a=rtcp-fb:96 nack pli\r\n", ""},
  };
  for (size_t i = 0; i < sizeof(changes) / sizeof(*changes); i++) {
    make_answer(answer, sizeof(answer), "a=setup:active\r\n");
    replace(answer, sizeof(answer), changes[i][0], changes[i][1]);
    check_media_rejected(answer);
  }
  make_answer(answer, sizeof(answer), "a=setup:active\r\n");
  *strstr(answer, "m=audio") = '\0';
  check_media_rejected(answer);
  make_answer(answer, sizeof(answer), "a=setup:active\r\n");
  strcat(answer, "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n");
  check_media_rejected(answer);
}

static void test_answer_roles(void) {
  char answer[CONFIG_SDP_BUFFER_SIZE], changed[CONFIG_SDP_BUFFER_SIZE];
  const char* roles[] = {"active", "passive"};
  for (size_t i = 0; i < sizeof(roles) / sizeof(*roles); i++) {
    make_answer(answer, sizeof(answer), "a=setup:active\r\n");
    replace(answer, sizeof(answer), "a=setup:active", i ? "a=setup:passive" : "a=setup:active");
    PeerConnection* pc = offerer();
    peer_connection_set_remote_description(pc, answer, SDP_TYPE_ANSWER);
    assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
    peer_connection_set_remote_description(pc, answer, SDP_TYPE_ANSWER);
    assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
    strcpy(changed, answer);
    replace(changed, sizeof(changed), roles[i], roles[1 - i]);
    peer_connection_set_remote_description(pc, changed, SDP_TYPE_ANSWER);
    assert(peer_connection_get_state(pc) == PEER_CONNECTION_FAILED);
    peer_connection_destroy(pc);
  }
  make_answer(answer, sizeof(answer), "");
  replace(answer, sizeof(answer), "a=mid:1\r\n", "a=mid:1\r\na=setup:active\r\n");
  replace(answer, sizeof(answer), "a=mid:2\r\n", "a=mid:2\r\na=setup:active\r\n");
  PeerConnection* pc = offerer();
  peer_connection_set_remote_description(pc, answer, SDP_TYPE_ANSWER);
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
  peer_connection_destroy(pc);
  replace(answer, sizeof(answer), "a=mid:2\r\na=setup:active", "a=mid:2\r\na=setup:passive");
  check_rejected(answer);

  /* Media-level setup overrides a legal session default. */
  make_answer(answer, sizeof(answer), "a=setup:passive\r\n");
  replace(answer, sizeof(answer), "a=mid:1\r\n", "a=mid:1\r\na=setup:active\r\n");
  replace(answer, sizeof(answer), "a=mid:2\r\n", "a=mid:2\r\na=setup:active\r\n");
  pc = offerer();
  peer_connection_set_remote_description(pc, answer, SDP_TYPE_ANSWER);
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_CHECKING);
  peer_connection_destroy(pc);

  const char* bad_setup[] = {"", "a=setup:actpass\r\n", "a=setup:holdconn\r\n",
      "a=setup:garbage\r\n", "a=setup:active\r\na=setup:passive\r\n"};
  for (size_t i = 0; i < sizeof(bad_setup) / sizeof(*bad_setup); i++) {
    make_answer(answer, sizeof(answer), bad_setup[i]);
    check_rejected(answer);
  }
  make_answer(answer, sizeof(answer), "a=setup:active\r\n");
  PeerConfiguration config = profile();
  pc = peer_connection_create(&config);
  assert(pc);
  peer_connection_set_remote_description(pc, answer, SDP_TYPE_ANSWER); /* Unsolicited. */
  assert(peer_connection_get_state(pc) == PEER_CONNECTION_FAILED);
  peer_connection_destroy(pc);
}

static void test_answer_bounds(void) {
  char answer[CONFIG_SDP_BUFFER_SIZE + 1];
  check_rejected(NULL);
  make_answer(answer, sizeof(answer), "a=setup:active\r\n");
  answer[strlen(answer) - 1] = '\0';
  check_rejected(answer); /* Unterminated final CRLF. */
  make_answer(answer, sizeof(answer), "a=setup:active\r\n");
  replace(answer, sizeof(answer), "a=mid:1\r\n", "a=mid:1\na=mid:1\r\n");
  check_rejected(answer); /* Bare LF inside a line. */
  make_answer(answer, sizeof(answer), "a=setup:active\r\n");
  char long_fingerprint[DTLS_SRTP_FINGERPRINT_LENGTH + 1];
  memset(long_fingerprint, 'F', sizeof(long_fingerprint) - 1);
  long_fingerprint[sizeof(long_fingerprint) - 1] = '\0';
  char* fingerprint = strstr(answer, "a=fingerprint:sha-256 ") + 22;
  char value[96];
  memcpy(value, fingerprint, 95);
  value[95] = '\0';
  replace(answer, sizeof(answer), value, long_fingerprint);
  check_rejected(answer);
  make_answer(answer, sizeof(answer), "a=setup:active\r\n");
  char line[259];
  memset(line, 'x', 256);
  memcpy(line + 256, "\r\n", 3);
  strcat(answer, line);
  check_rejected(answer);
  memset(answer, 'x', CONFIG_SDP_BUFFER_SIZE);
  answer[CONFIG_SDP_BUFFER_SIZE] = '\0';
  check_rejected(answer);
}

static int unused_send(void* ctx, const uint8_t* data, size_t size) {
  (void)ctx; (void)data;
  return (int)size;
}

static int unused_recv(void* ctx, uint8_t* data, size_t size) {
  (void)ctx; (void)data; (void)size;
  return MBEDTLS_ERR_SSL_TIMEOUT;
}

static void test_dtls_role(void) {
  DtlsSrtp dtls = {0};
  assert(dtls_srtp_set_role(&dtls, DTLS_SRTP_ROLE_CLIENT) != 0);
  assert(dtls_srtp_init(&dtls, DTLS_SRTP_ROLE_SERVER, &dtls) == 0);
  char fingerprint[DTLS_SRTP_FINGERPRINT_LENGTH];
  uint8_t cert[4096];
  size_t cert_size = dtls.cert.raw.len;
  size_t key_bits = mbedtls_pk_get_bitlen(&dtls.pkey);
  assert(cert_size > 0 && cert_size <= sizeof(cert));
  memcpy(cert, dtls.cert.raw.p, cert_size);
  strcpy(fingerprint, dtls.local_fingerprint);
  dtls.udp_send = unused_send;
  dtls.udp_recv = unused_recv;
  assert(dtls_srtp_set_role(&dtls, DTLS_SRTP_ROLE_SERVER) == 0);
  assert(dtls_srtp_set_role(&dtls, DTLS_SRTP_ROLE_CLIENT) == 0);
  assert(dtls.role == DTLS_SRTP_ROLE_CLIENT);
#if CONFIG_MBEDTLS_2_X
  assert(dtls.conf.endpoint == MBEDTLS_SSL_IS_CLIENT);
#else
  assert(dtls.conf.MBEDTLS_PRIVATE(endpoint) == MBEDTLS_SSL_IS_CLIENT);
#endif
  assert(!strcmp(fingerprint, dtls.local_fingerprint));
  assert(dtls.cert.raw.len == cert_size && !memcmp(cert, dtls.cert.raw.p, cert_size));
  assert(mbedtls_pk_get_bitlen(&dtls.pkey) == key_bits);
  assert(dtls.user_data == &dtls && dtls.udp_send == unused_send && dtls.udp_recv == unused_recv);
  assert(dtls_srtp_set_role(&dtls, DTLS_SRTP_ROLE_SERVER) == 0);
  assert(dtls.role == DTLS_SRTP_ROLE_SERVER && !strcmp(fingerprint, dtls.local_fingerprint));
  assert(dtls_srtp_set_role(&dtls, (DtlsSrtpRole)-1) != 0);
  dtls.state = DTLS_SRTP_STATE_HANDSHAKE;
  assert(dtls_srtp_set_role(&dtls, DTLS_SRTP_ROLE_CLIENT) != 0);
  dtls.state = DTLS_SRTP_STATE_CONNECTED;
  assert(dtls_srtp_set_role(&dtls, DTLS_SRTP_ROLE_CLIENT) != 0);
  dtls_srtp_deinit(&dtls);
  dtls_srtp_deinit(&dtls);
}

int main(void) {
  test_offer();
  test_answer_media();
  test_answer_roles();
  test_answer_bounds();
  test_dtls_role();
#if CONFIG_STRICT_CHROME_PROFILE
  puts("Strict native offer/answer and DTLS role regression: PASS");
#else
  puts("Generic offer standards and DTLS role regression: PASS");
#endif
  return 0;
}
