#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "rtcp.h"

int main(void) {
  static const uint8_t expected[] = {
      0x81,
      0xce,
      0x00,
      0x02,
      0x11,
      0x22,
      0x33,
      0x44,
      0x55,
      0x66,
      0x77,
      0x88,
  };
  uint8_t packet[12];

  assert(rtcp_get_pli(NULL, 12, 1, 2) < 0);
  assert(rtcp_get_pli(packet, 11, 1, 2) < 0);
  assert(rtcp_get_pli(packet, 12, 0, 2) < 0);
  assert(rtcp_get_pli(packet, 12, 1, 0) < 0);
  assert(rtcp_get_pli(packet, sizeof(packet), 0x11223344, 0x55667788) ==
         sizeof(packet));
  assert(memcmp(packet, expected, sizeof(expected)) == 0);
  assert(rtcp_probe(packet, sizeof(packet)) != 0);
  return 0;
}
