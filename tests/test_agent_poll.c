#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/select.h>

static int poll_error;
static int test_select(int nfds, fd_set* readfds, fd_set* writefds,
                       fd_set* exceptfds, struct timeval* timeout) {
  (void)nfds; (void)readfds; (void)writefds; (void)exceptfds;
  assert(timeout->tv_sec == 0 && timeout->tv_usec == 1000);
  errno = poll_error;
  return poll_error ? -1 : 0;
}

/* Inject the interrupted syscall without signal-timing races. All other agent
 * code and its transport dependencies are the real library implementation. */
#define select test_select
#include "../src/agent.c"
#undef select

int main(void) {
  Agent agent = {0};
  uint8_t data[128];
  agent.udp_sockets[0].fd = 1;
  agent.udp_sockets[1].fd = 2;
  poll_error = EINTR;
  assert(agent_recv(&agent, data, sizeof(data)) == 0);
  poll_error = EBADF;
  assert(agent_recv(&agent, data, sizeof(data)) == -1);
  poll_error = 0;
  assert(agent_recv(&agent, data, sizeof(data)) == 0);
  puts("Interrupted agent poll regression: PASS");
  return 0;
}
