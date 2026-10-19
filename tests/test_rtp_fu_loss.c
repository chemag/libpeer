#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rtp.h"

static uint8_t received[64];
static size_t received_size;
static unsigned int received_packets;

static void on_packet(uint8_t* data, size_t size, void* user_data) {
  (void)user_data;
  assert(size <= sizeof(received));
  memcpy(received, data, size);
  received_size = size;
  received_packets++;
}

static int decode_fu(RtpDecoder* decoder, uint16_t sequence, uint32_t timestamp, uint8_t fu_header, const uint8_t* payload, size_t payload_size) {
  uint8_t packet[32] = {0};

  assert(payload_size <= sizeof(packet) - sizeof(RtpHeader) - 2);
  packet[0] = 0x80;
  packet[1] = PT_H264;
  packet[2] = (uint8_t)(sequence >> 8);
  packet[3] = (uint8_t)sequence;
  packet[4] = (uint8_t)(timestamp >> 24);
  packet[5] = (uint8_t)(timestamp >> 16);
  packet[6] = (uint8_t)(timestamp >> 8);
  packet[7] = (uint8_t)timestamp;
  packet[12] = 0x7c; /* FU-A, NRI 3. */
  packet[13] = fu_header;
  memcpy(packet + 14, payload, payload_size);
  return rtp_decoder_decode(decoder, packet, 14 + payload_size);
}

static int decode_single(RtpDecoder* decoder, uint16_t sequence, uint32_t timestamp) {
  uint8_t packet[14] = {0};

  packet[0] = 0x80;
  packet[1] = PT_H264;
  packet[2] = (uint8_t)(sequence >> 8);
  packet[3] = (uint8_t)sequence;
  packet[4] = (uint8_t)(timestamp >> 24);
  packet[5] = (uint8_t)(timestamp >> 16);
  packet[6] = (uint8_t)(timestamp >> 8);
  packet[7] = (uint8_t)timestamp;
  packet[12] = 0x61;
  packet[13] = 0x55;
  return rtp_decoder_decode(decoder, packet, sizeof(packet));
}

int main(void) {
  static const uint8_t first[] = {0xaa};
  static const uint8_t last[] = {0xbb};
  static const uint8_t expected[] = {0, 0, 0, 1, 0x65, 0xaa, 0xbb};
  RtpDecoder decoder = {0};

  rtp_decoder_init(&decoder, CODEC_H264, on_packet, NULL);

  assert(decode_fu(&decoder, 100, 1234, 0x85, first, sizeof(first)) == 0);
  assert(decode_fu(&decoder, 101, 1234, 0x45, last, sizeof(last)) == 0);
  assert(received_packets == 1);
  assert(received_size == sizeof(expected));
  assert(memcmp(received, expected, sizeof(expected)) == 0);

  assert(decode_fu(&decoder, 200, 2000, 0x85, first, sizeof(first)) == 0);
  assert(decode_fu(&decoder, 202, 2000, 0x45, last, sizeof(last)) < 0);
  assert(received_packets == 1);
  assert(decode_fu(&decoder, 203, 2000, 0x45, last, sizeof(last)) < 0);
  assert(received_packets == 1);

  assert(decode_fu(&decoder, 300, 3000, 0x85, first, sizeof(first)) == 0);
  assert(decode_fu(&decoder, 301, 3001, 0x45, last, sizeof(last)) < 0);
  assert(received_packets == 1);

  assert(decode_fu(&decoder, UINT16_MAX, 4000, 0x85, first,
                   sizeof(first)) == 0);
  assert(decode_fu(&decoder, 0, 4000, 0x45, last, sizeof(last)) == 0);
  assert(received_packets == 2);

  assert(decode_fu(&decoder, 500, 5000, 0x85, first, sizeof(first)) == 0);
  assert(decode_single(&decoder, 501, 5000) == 0);
  assert(received_packets == 3);
  assert(decode_fu(&decoder, 502, 5000, 0x45, last, sizeof(last)) < 0);
  assert(received_packets == 3);

  puts("H.264 FU-A sequence-gap regression: PASS");
  return 0;
}
