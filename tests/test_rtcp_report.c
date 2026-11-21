#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rtcp.h"

int main(void) {
  static const uint8_t expected_sr[] = {
      /* SR, one report block, length 12 */
      0x81, 0xc8, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x05,
      0x00, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x2e, 0xe0, 0x00, 0x00, 0x00, 0x4b,
      0x00, 0x00, 0x2e, 0xe0,
      0x11, 0x22, 0x33, 0x44, 0x33, 0x00, 0x00, 0x01,
      0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x02, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00,
      /* SDES, CNAME "libpeer" */
      0x81, 0xca, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05,
      0x01, 0x07, 0x6c, 0x69, 0x62, 0x70, 0x65, 0x65,
      0x72, 0x00, 0x00, 0x00,
  };
  static const uint8_t expected_rr[] = {
      0x80, 0xc9, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05,
      0x81, 0xca, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05,
      0x01, 0x07, 0x6c, 0x69, 0x62, 0x70, 0x65, 0x65,
      0x72, 0x00, 0x00, 0x00,
  };
  uint8_t packet[128];
  RtcpSenderInfo sender = {
      .rtp_timestamp = 12000,
      .packet_count = 75,
      .octet_count = 12000,
  };
  RtcpSenderInfo parsed;
  RtcpReceiver receiver;
  RtcpReportBlock block;
  uint32_t ssrc = 0;

  /* 1.5 s is 1 in the seconds and one half in the fraction. */
  assert(rtcp_ntp_timestamp(1500000) == 0x0000000180000000ULL);
  sender.ntp_timestamp = rtcp_ntp_timestamp(1500000);

  /* Sequence numbers 65534, 65535, 0 and 2: one wrap, one packet lost. */
  memset(&receiver, 0, sizeof(receiver));
  rtcp_receiver_on_packet(&receiver, 0x11223344, 65534);
  rtcp_receiver_on_packet(&receiver, 0x11223344, 65535);
  rtcp_receiver_on_packet(&receiver, 0x11223344, 0);
  rtcp_receiver_on_packet(&receiver, 0x11223344, 2);
  rtcp_receiver_on_sender_report(&receiver, 0x0000000280000000ULL, 1000000);
  rtcp_receiver_get_report_block(&receiver, 1500000, &block);
  assert(block.ssrc == 0x11223344);
  assert(block.flcnpl == ((256U / 5U) << 24 | 1U)); /* 1 of 5 expected */
  assert(block.ehsnr == 0x00010002);
  assert(block.jitter == 0);
  assert(block.lsr == 0x00028000);
  assert(block.dlsr == 0x00008000); /* half a second */

  assert(rtcp_get_report(packet, sizeof(packet), 5, &sender, &block, "libpeer") ==
         (int)sizeof(expected_sr));
  assert(memcmp(packet, expected_sr, sizeof(expected_sr)) == 0);
  assert(rtcp_parse_sender_report(packet, sizeof(expected_sr), &ssrc, &parsed) == 0);
  assert(ssrc == 5);
  assert(parsed.ntp_timestamp == sender.ntp_timestamp);
  assert(parsed.rtp_timestamp == 12000);
  assert(parsed.packet_count == 75 && parsed.octet_count == 12000);

  assert(rtcp_get_report(packet, sizeof(packet), 5, NULL, NULL, "libpeer") ==
         (int)sizeof(expected_rr));
  assert(memcmp(packet, expected_rr, sizeof(expected_rr)) == 0);
  assert(rtcp_parse_sender_report(packet, sizeof(expected_rr), &ssrc, &parsed) < 0);

  assert(rtcp_get_report(NULL, sizeof(packet), 5, &sender, NULL, "libpeer") < 0);
  assert(rtcp_get_report(packet, sizeof(packet), 0, &sender, NULL, "libpeer") < 0);
  assert(rtcp_get_report(packet, sizeof(packet), 5, &sender, NULL, "") < 0);
  assert(rtcp_get_report(packet, 47, 5, &sender, NULL, "libpeer") < 0);
  assert(rtcp_get_report(packet, 48, 5, &sender, NULL, "libpeer") == 48);

  /* Nothing lost in the next interval, the cumulative number stays. A
   * duplicate then makes the interval negative, which reports no loss. */
  rtcp_receiver_on_packet(&receiver, 0x11223344, 3);
  rtcp_receiver_get_report_block(&receiver, 2000000, &block);
  assert(block.flcnpl == 1U);
  assert(block.ehsnr == 0x00010003);
  assert(block.dlsr == 0x00010000);
  rtcp_receiver_on_packet(&receiver, 0x11223344, 3);
  rtcp_receiver_on_packet(&receiver, 0x11223344, 3);
  rtcp_receiver_get_report_block(&receiver, 2000000, &block);
  assert(block.flcnpl == 0x00ffffffU); /* -1 in 24 bits */

  /* Another SSRC is another stream. */
  rtcp_receiver_on_packet(&receiver, 0x55667788, 100);
  rtcp_receiver_get_report_block(&receiver, 2000000, &block);
  assert(block.ssrc == 0x55667788);
  assert(block.flcnpl == 0 && block.ehsnr == 100);
  assert(block.lsr == 0 && block.dlsr == 0);

  printf("RTCP reports: PASS\n");
  return 0;
}
