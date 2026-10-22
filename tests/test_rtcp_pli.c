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
  static const uint8_t expected_compound[] = {
      0x80, 0xc9, 0x00, 0x01, 0x11, 0x22, 0x33, 0x44,
      0x81, 0xca, 0x00, 0x04, 0x11, 0x22, 0x33, 0x44,
      0x01, 0x07, 0x6c, 0x69, 0x62, 0x70, 0x65, 0x65,
      0x72, 0x00, 0x00, 0x00,
      0x81, 0xce, 0x00, 0x02, 0x11, 0x22, 0x33, 0x44,
      0x55, 0x66, 0x77, 0x88,
  };
  uint8_t packet[12];
  uint8_t compound[128];
  char too_long_cname[257];

  assert(rtcp_get_pli(NULL, 12, 1, 2) < 0);
  assert(rtcp_get_pli(packet, 11, 1, 2) < 0);
  assert(rtcp_get_pli(packet, 12, 0, 2) < 0);
  assert(rtcp_get_pli(packet, 12, 1, 0) < 0);
  assert(rtcp_get_pli(packet, sizeof(packet), 0x11223344, 0x55667788) ==
         sizeof(packet));
  assert(memcmp(packet, expected, sizeof(expected)) == 0);
  assert(rtcp_probe(packet, sizeof(packet)) != 0);

  memset(too_long_cname, 'x', sizeof(too_long_cname) - 1);
  too_long_cname[sizeof(too_long_cname) - 1] = '\0';
  assert(rtcp_get_compound_pli(NULL, sizeof(compound), 1, 2, "libpeer") < 0);
  assert(rtcp_get_compound_pli(compound, sizeof(compound), 0, 2, "libpeer") <
         0);
  assert(rtcp_get_compound_pli(compound, sizeof(compound), 1, 0, "libpeer") <
         0);
  assert(rtcp_get_compound_pli(compound, sizeof(compound), 1, 2, NULL) < 0);
  assert(rtcp_get_compound_pli(compound, sizeof(compound), 1, 2, "") < 0);
  assert(rtcp_get_compound_pli(compound, sizeof(compound), 1, 2,
                               too_long_cname) < 0);
  assert(rtcp_get_compound_pli(compound, sizeof(expected_compound) - 1,
                               0x11223344, 0x55667788, "libpeer") < 0);
  assert(rtcp_get_compound_pli(compound, sizeof(compound), 0x11223344,
                               0x55667788, "libpeer") ==
         sizeof(expected_compound));
  assert(memcmp(compound, expected_compound, sizeof(expected_compound)) == 0);
  assert(rtcp_probe(compound, sizeof(expected_compound)) != 0);
  return 0;
}
