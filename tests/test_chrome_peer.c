#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dtls_srtp.h"
#include "peer_connection.h"

static PeerConnection* peer;
static int changes;
static void on_state(PeerConnectionState state, void* unused) {
  (void)unused;
  assert(peer_connection_get_state(peer) == state);
  changes++;
}

static const char offer[] =
    "v=0\r\na=group:BUNDLE 0 1\r\n"
    "m=audio 9 UDP/TLS/RTP/SAVPF 0\r\n"
    "a=mid:0\r\na=sendrecv\r\na=rtcp-mux\r\na=rtpmap:0 PCMU/8000\r\n"
    "a=ice-ufrag:test\r\na=ice-pwd:abcdefghijklmnopqrstuvwx\r\n"
    "m=video 9 UDP/TLS/RTP/SAVPF 125\r\na=mid:1\r\na=sendrecv\r\na=rtcp-mux\r\n"
    "a=rtpmap:125 H264/90000\r\na=fmtp:125 packetization-mode=1;profile-level-id=42e01f\r\n"
    "a=rtcp-fb:125 nack pli\r\n";

int main(void) {
  PeerConfiguration config = {0};
  config.audio_codec = CODEC_PCMU;
  config.video_codec = CODEC_H264;
  peer = peer_connection_create(&config);
  assert(peer);
  peer_connection_set_remote_description(peer, offer, SDP_TYPE_OFFER);
  assert(peer_connection_get_state(peer) == PEER_CONNECTION_CHECKING); /* No callback. */
  peer_connection_close(peer);
  assert(peer_connection_get_state(peer) == PEER_CONNECTION_CLOSED);
  peer_connection_destroy(peer);
  peer = peer_connection_create(&config);
  peer_connection_oniceconnectionstatechange(peer, on_state);
  peer_connection_set_remote_description(peer, offer, SDP_TYPE_OFFER);
  assert(changes == 1);
  peer_connection_close(peer);
  assert(changes == 2);
  peer_connection_destroy(peer);
  peer = peer_connection_create(&config);
  peer_connection_set_remote_description(peer, "m=audio", SDP_TYPE_OFFER);
#if CONFIG_STRICT_CHROME_PROFILE
  assert(peer_connection_get_state(peer) == PEER_CONNECTION_FAILED);
  assert(peer_connection_create_answer(peer) == NULL);
#else
  assert(peer_connection_get_state(peer) == PEER_CONNECTION_CHECKING); /* Legacy parsing retained. */
#endif
  peer_connection_destroy(peer);

  /* Public API rejects NULL before the strict offer parser is entered. */
  peer = peer_connection_create(&config);
  assert(peer);
  peer_connection_set_remote_description(peer, NULL, SDP_TYPE_OFFER);
  assert(peer_connection_get_state(peer) == PEER_CONNECTION_FAILED);
  assert(peer_connection_create_answer(peer) == NULL);
  peer_connection_destroy(peer);

  assert(srtp_init() == srtp_err_status_ok);
  DtlsSrtp dtls = {0};
  uint8_t packet[64] = {0x80, 0, 0, 1, 0, 0, 0, 160, 0, 0, 0, 5, 0xff};
  int size = 13;
  assert(dtls_srtp_decrypt_rtp_packet(&dtls, packet, &size) != 0);
  assert(dtls_srtp_encrypt_rtp_packet(&dtls, packet, &size) != 0);
  uint8_t key[30] = {1};
  srtp_policy_t policy = {0};
  srtp_crypto_policy_set_rtp_default(&policy.rtp);
  srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
  policy.key = key;
  policy.ssrc.type = ssrc_any_outbound;
  assert(srtp_create(&dtls.srtp_out, &policy) == srtp_err_status_ok);
  policy.ssrc.type = ssrc_any_inbound;
  assert(srtp_create(&dtls.srtp_in, &policy) == srtp_err_status_ok);
  dtls.state = DTLS_SRTP_STATE_CONNECTED;
  assert(dtls_srtp_encrypt_rtp_packet(&dtls, packet, &size) == 0);
  uint8_t valid[64], replay[64];
  memcpy(valid, packet, size);
  memcpy(replay, packet, size);
  int valid_size = size, replay_size = size;
  packet[size - 1] ^= 1;
  assert(dtls_srtp_decrypt_rtp_packet(&dtls, packet, &size) != 0);
  assert(dtls_srtp_decrypt_rtp_packet(&dtls, valid, &valid_size) == 0 && valid_size == 13);
  assert(valid[12] == 0xff);
  assert(dtls_srtp_decrypt_rtp_packet(&dtls, replay, &replay_size) == srtp_err_status_replay_fail);
  srtp_dealloc(dtls.srtp_in);
  srtp_dealloc(dtls.srtp_out);
  srtp_shutdown();
  puts("Chrome peer state/SRTP regression: PASS");
  return 0;
}
