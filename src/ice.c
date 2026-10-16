#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "ice.h"
#include "mdns.h"
#include "ports.h"
#include "socket.h"
#include "utils.h"

static uint8_t ice_candidate_type_preference(IceCandidateType type) {
  switch (type) {
    case ICE_CANDIDATE_TYPE_HOST:
      return 126;
    case ICE_CANDIDATE_TYPE_SRFLX:
      return 100;
    case ICE_CANDIDATE_TYPE_RELAY:
      return 0;
    default:
      return 0;
  }
}

static uint16_t ice_candidate_local_preference(IceCandidate* candidate) {
  return candidate->addr.port;
}

static void ice_candidate_priority(IceCandidate* candidate) {
  // priority = (2^24)*(type preference) + (2^8)*(local preference) + (256 - component ID)
  candidate->priority = (1 << 24) * ice_candidate_type_preference(candidate->type) + (1 << 8) * ice_candidate_local_preference(candidate) + (256 - candidate->component);
}

void ice_candidate_create(IceCandidate* candidate, int foundation, IceCandidateType type, Address* addr) {
  memcpy(&candidate->addr, addr, sizeof(Address));
  candidate->type = type;

  snprintf(candidate->foundation, sizeof(candidate->foundation), "%d", foundation);
  // 1: RTP, 2: RTCP
  candidate->component = 1;

  ice_candidate_priority(candidate);

  snprintf(candidate->transport, sizeof(candidate->transport), "%s", "UDP");
}

void ice_candidate_to_description(IceCandidate* candidate, char* description, int length) {
  char addr_string[ADDRSTRLEN];
  char typ_raddr[128];

  memset(typ_raddr, 0, sizeof(typ_raddr));
  addr_to_string(&candidate->raddr, addr_string, sizeof(addr_string));

  switch (candidate->type) {
    case ICE_CANDIDATE_TYPE_HOST:
      snprintf(typ_raddr, sizeof(typ_raddr), "host");
      break;
    case ICE_CANDIDATE_TYPE_SRFLX:
      snprintf(typ_raddr, sizeof(typ_raddr), "srflx raddr %s rport %d", addr_string, candidate->raddr.port);
      break;
    case ICE_CANDIDATE_TYPE_RELAY:
      snprintf(typ_raddr, sizeof(typ_raddr), "relay raddr %s rport %d", addr_string, candidate->raddr.port);
    default:
      break;
  }

  addr_to_string(&candidate->addr, addr_string, sizeof(addr_string));
  snprintf(description, length, "a=candidate:%s %d %s %" PRIu32 " %s %d typ %s\r\n",
           candidate->foundation,
           candidate->component,
           candidate->transport,
           candidate->priority,
           addr_string,
           candidate->addr.port,
           typ_raddr);
}

static int ice_read_token(char** input, char* output, size_t capacity) {
  char* p = *input;
  while (isspace((unsigned char)*p)) p++;
  char* end = p;
  while (*end && !isspace((unsigned char)*end)) end++;
  size_t length = end - p;
  if (!length || length >= capacity) return -1;
  memcpy(output, p, length);
  output[length] = '\0';
  *input = end;
  return 0;
}

static int ice_read_number(const char* text, uint32_t maximum, uint32_t* number) {
  char* end;
  if (!isdigit((unsigned char)*text)) return -1;
  errno = 0;
  unsigned long value = strtoul(text, &end, 10);
  if (errno || *end || value > maximum) return -1;
  *number = (uint32_t)value;
  return 0;
}

int ice_candidate_from_description(IceCandidate* candidate, char* description, char* end) {
  char line[512], component[12], priority[12], port_text[12], typ[4];
  char type[16], addrstring[ADDRSTRLEN];
  uint32_t port, component_number;
  IceCandidate parsed = {0};
  if (!candidate || !description || !end || end <= description ||
      (size_t)(end - description) >= sizeof(line) || memchr(description, '\0', end - description)) return -1;
  memcpy(line, description, end - description);
  line[end - description] = '\0';
  char* candidate_start = line;
  if (strncmp(candidate_start, "a=", 2) == 0) candidate_start += 2;
  if (strncmp(candidate_start, "candidate:", 10) != 0) return -1;
  candidate_start += 10;
  /* Bound every token independently; parsing never reads beyond the supplied line. */
  if (ice_read_token(&candidate_start, parsed.foundation, sizeof(parsed.foundation)) ||
      ice_read_token(&candidate_start, component, sizeof(component)) ||
      ice_read_token(&candidate_start, parsed.transport, sizeof(parsed.transport)) ||
      ice_read_token(&candidate_start, priority, sizeof(priority)) ||
      ice_read_token(&candidate_start, addrstring, sizeof(addrstring)) ||
      ice_read_token(&candidate_start, port_text, sizeof(port_text)) ||
      ice_read_token(&candidate_start, typ, sizeof(typ)) ||
      ice_read_token(&candidate_start, type, sizeof(type)) || strcmp(typ, "typ") ||
      ice_read_number(component, 256, &component_number) || !component_number ||
      ice_read_number(priority, UINT32_MAX, &parsed.priority) ||
      ice_read_number(port_text, UINT16_MAX, &port) || !port) return -1;
  parsed.component = (int)component_number;
  if (strcmp(parsed.transport, "UDP") != 0 && strcmp(parsed.transport, "udp") != 0) return -1;
  if (strcmp(type, "host") == 0) parsed.type = ICE_CANDIDATE_TYPE_HOST;
  else if (strcmp(type, "srflx") == 0) parsed.type = ICE_CANDIDATE_TYPE_SRFLX;
  else if (strcmp(type, "relay") == 0) parsed.type = ICE_CANDIDATE_TYPE_RELAY;
  else return -1;
  addr_set_port(&parsed.addr, (uint16_t)port);
  if (strstr(addrstring, "local") != NULL) {
    if (mdns_resolve_addr(addrstring, &parsed.addr) == 0) return -1;
  } else if (addr_from_string(addrstring, &parsed.addr) == 0) return -1;
  *candidate = parsed;
  return 0;
}
