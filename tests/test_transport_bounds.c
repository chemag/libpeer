#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "agent.h"
#include "dtls_srtp.h"
#include "peer_connection.h"

static void check_candidate_tokens(void) {
  struct Guarded { unsigned char before[16]; IceCandidate candidate; unsigned char after[16]; } guarded;
  memset(&guarded, 0xa5, sizeof(guarded));
  IceCandidate original = guarded.candidate;
  char line[512], long_token[200];
  memset(long_token, 'x', sizeof(long_token) - 1); long_token[sizeof(long_token) - 1] = 0;
  const char* formats[] = {
    "candidate:%s 1 UDP 1 127.0.0.1 1234 typ host",
    "candidate:1 1 %s 1 127.0.0.1 1234 typ host",
    "candidate:1 1 UDP 1 %s 1234 typ host",
    "candidate:1 1 UDP 1 127.0.0.1 1234 typ %s",
    "candidate:1 %s UDP 1 127.0.0.1 1234 typ host",
    "candidate:1 1 UDP %s 127.0.0.1 1234 typ host",
    "candidate:1 1 UDP 1 127.0.0.1 %s typ host"
  };
  for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); i++) {
    snprintf(line, sizeof(line), formats[i], long_token);
    assert(ice_candidate_from_description(&guarded.candidate, line, line + strlen(line)) < 0);
    assert(!memcmp(&original, &guarded.candidate, sizeof(original)));
  }
  for (size_t i = 0; i < sizeof(guarded.before); i++) assert(guarded.before[i] == 0xa5 && guarded.after[i] == 0xa5);
  strcpy(line, "a=candidate:good 1 UDP 4294967295 127.0.0.1 65535 typ host");
  char* end = line + strlen(line);
  strcpy(end, "\r\nthis is outside the supplied span");
  assert(ice_candidate_from_description(&guarded.candidate, line, end) == 0);
  assert(!strcmp(guarded.candidate.foundation, "good") && guarded.candidate.addr.port == 65535);
  assert(ice_candidate_from_description(&guarded.candidate, line, line + 5) < 0);
  strcpy(line, "candidate:1 1 UDP 4294967296 127.0.0.1 1234 typ host");
  assert(ice_candidate_from_description(&guarded.candidate, line, line + strlen(line)) < 0);
}

static void check_candidate_capacity(void) {
  Agent agent = {0};
  char candidate[] = "candidate:one 1 UDP 1 127.0.0.1 1234 typ host";
  for (int i = 0; i < AGENT_MAX_CANDIDATES; i++) assert(agent_add_remote_candidate(&agent, candidate, candidate + strlen(candidate), 0) == 0);
  Agent before = agent;
  assert(agent_add_remote_candidate(&agent, candidate, candidate + strlen(candidate), 0) < 0);
  assert(!memcmp(&agent, &before, sizeof(agent)));
  char sdp[128]; snprintf(sdp, sizeof(sdp), "a=%s\r\n", candidate);
  agent_set_remote_description(&agent, sdp);
  assert(!memcmp(&agent, &before, sizeof(agent)));
  agent.local_candidates_count = AGENT_MAX_CANDIDATES;
  before = agent;
  agent_gather_candidate(&agent, NULL, NULL, NULL);
  agent_gather_candidate(&agent, "stun:invalid:1", NULL, NULL);
  agent_gather_candidate(&agent, "turn:invalid:1", "user", "secret");
  assert(!memcmp(&agent, &before, sizeof(agent)));
  memset(&agent, 0, sizeof(agent));
  assert(agent_add_remote_candidate(&agent, candidate, candidate + strlen(candidate), 1) == 0);
  assert(agent_add_remote_candidate(&agent, candidate, candidate + strlen(candidate), 1) == 0);
  assert(agent.remote_candidates_count == 1);
  PeerConfiguration config = {0};
  PeerConnection* pc = peer_connection_create(&config);
  assert(pc);
  for (int i = 0; i < AGENT_MAX_CANDIDATES; i++) assert(peer_connection_add_ice_candidate(pc, candidate) == 0);
  assert(peer_connection_add_ice_candidate(pc, candidate) < 0);
  peer_connection_destroy(pc);
}

static void check_stun_copy(void) {
  Agent agent = {0};
  assert(agent_create(&agent) == 0);
  int sender = socket(AF_INET, SOCK_DGRAM, 0);
  assert(sender >= 0);
  struct sockaddr_in destination = {0};
  destination.sin_family = AF_INET;
  destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  destination.sin_port = htons(agent.udp_sockets[0].bind_addr.port);
  unsigned char data[512] = {0}, received[1300];
  data[1] = 0x11; /* Binding indication: no response or credentials needed. */
  data[4] = 0x21; data[5] = 0x12; data[6] = 0xa4; data[7] = 0x42;
  data[2] = 1; data[3] = 0xec; /* 492-byte body exceeds the fixed message buffer. */
  assert(sendto(sender, data, sizeof(data), 0, (struct sockaddr*)&destination, sizeof(destination)) == sizeof(data));
  assert(agent_recv(&agent, received, sizeof(received)) == 0);
  assert(recv(agent.udp_sockets[0].fd, received, sizeof(received), MSG_PEEK | MSG_DONTWAIT) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
  data[2] = data[3] = 0;
  assert(sendto(sender, data, 20, 0, (struct sockaddr*)&destination, sizeof(destination)) == 20);
  assert(agent_recv(&agent, received, sizeof(received)) == 0);
  assert(sendto(sender, data, 12, 0, (struct sockaddr*)&destination, sizeof(destination)) == 12);
  assert(agent_recv(&agent, received, sizeof(received)) == 12); /* Actual received length governs probe. */
  data[3] = 132; data[20] = 0; data[21] = STUN_ATTR_TYPE_MESSAGE_INTEGRITY; data[22] = 0; data[23] = 128;
  assert(sendto(sender, data, 152, 0, (struct sockaddr*)&destination, sizeof(destination)) == 152);
  assert(agent_recv(&agent, received, sizeof(received)) == 0); /* Invalid copied attribute length. */
  close(sender);
  agent_destroy(&agent);
}

static void check_dtls_nonzero_storage(void) {
  DtlsSrtp dtls;
  memset(&dtls, 0xa5, sizeof(dtls));
  assert(dtls_srtp_init(&dtls, (DtlsSrtpRole)-1, NULL) != 0);
  assert(dtls.initialized && !dtls.srtp_in && !dtls.srtp_out && !dtls.remote_addr);
  dtls_srtp_deinit(&dtls);
  dtls_srtp_deinit(&dtls);
  memset(&dtls, 0xa5, sizeof(dtls));
  assert(dtls_srtp_init(&dtls, DTLS_SRTP_ROLE_CLIENT, NULL) == 0);
  assert(dtls.initialized && !dtls.srtp_in && !dtls.srtp_out);
  dtls_srtp_deinit(&dtls);
}

int main(void) {
  check_candidate_tokens();
  check_candidate_capacity();
  check_stun_copy();
  check_dtls_nonzero_storage();
  puts("ICE/STUN/DTLS initialization bounds regression: PASS");
  return 0;
}
