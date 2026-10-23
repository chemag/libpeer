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

static int decode_fu(RtpDecoder* decoder, uint16_t sequence,
                     uint32_t timestamp, uint8_t fu_header,
                     const uint8_t* payload, size_t payload_size) {
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

static int decode_single(RtpDecoder* decoder, uint16_t sequence,
                         uint32_t timestamp, uint8_t nalu_header) {
  uint8_t packet[14] = {0};

  packet[0] = 0x80;
  packet[1] = PT_H264;
  packet[2] = (uint8_t)(sequence >> 8);
  packet[3] = (uint8_t)sequence;
  packet[4] = (uint8_t)(timestamp >> 24);
  packet[5] = (uint8_t)(timestamp >> 16);
  packet[6] = (uint8_t)(timestamp >> 8);
  packet[7] = (uint8_t)timestamp;
  packet[12] = nalu_header;
  packet[13] = 0x55;
  return rtp_decoder_decode(decoder, packet, sizeof(packet));
}

static void reset_decoder(RtpDecoder* decoder) {
  memset(decoder, 0, sizeof(*decoder));
  received_size = 0;
  received_packets = 0;
  rtp_decoder_init(decoder, CODEC_H264, on_packet, NULL);
}

int main(void) {
  static const uint8_t first[] = {0xaa};
  static const uint8_t last[] = {0xbb};
  static const uint8_t expected[] = {0, 0, 0, 1, 0x65, 0xaa, 0xbb};
  RtpDecoder decoder;

  reset_decoder(&decoder);
  assert(decode_fu(&decoder, 100, 1234, 0x85, first, sizeof(first)) == 0);
  assert(decode_fu(&decoder, 101, 1234, 0x45, last, sizeof(last)) == 0);
  assert(received_packets == 1);
  assert(received_size == sizeof(expected));
  assert(memcmp(received, expected, sizeof(expected)) == 0);

  reset_decoder(&decoder);
  assert(decode_fu(&decoder, 200, 2000, 0x81, first, sizeof(first)) == 0);
  assert(decode_fu(&decoder, 202, 2000, 0x41, last, sizeof(last)) < 0);
  assert(decoder.h264_waiting_for_idr == 1);
  assert(decode_fu(&decoder, 203, 2000, 0x41, last, sizeof(last)) == 0);
  assert(received_packets == 0);

  reset_decoder(&decoder);
  assert(decode_fu(&decoder, 300, 3000, 0x81, first, sizeof(first)) == 0);
  assert(decode_fu(&decoder, 301, 3001, 0x41, last, sizeof(last)) < 0);
  assert(received_packets == 0);

  reset_decoder(&decoder);
  assert(decode_fu(&decoder, UINT16_MAX, 4000, 0x85, first,
                   sizeof(first)) == 0);
  assert(decode_fu(&decoder, 0, 4000, 0x45, last, sizeof(last)) == 0);
  assert(received_packets == 1);

  reset_decoder(&decoder);
  assert(decode_single(&decoder, 100, 4500, 0x65) == 0);
  assert(decode_single(&decoder, 102, 4502, 0x61) < 0);
  assert(decoder.h264_waiting_for_idr == 1);
  assert(received_packets == 1);
  assert(decode_single(&decoder, 103, 4503, 0x61) == 0);
  assert(received_packets == 1);
  assert(decode_single(&decoder, 104, 4504, 0x65) == 0);
  assert(decoder.h264_waiting_for_idr == 0);
  assert(received_packets == 2);

  reset_decoder(&decoder);
  assert(decode_fu(&decoder, 500, 5000, 0x81, first, sizeof(first)) == 0);
  assert(decode_single(&decoder, 501, 5001, 0x61) < 0);
  assert(decoder.h264_waiting_for_idr == 1);
  assert(received_packets == 0);
  assert(decode_single(&decoder, 502, 5002, 0x61) == 0);
  assert(received_packets == 0);
  assert(decode_single(&decoder, 503, 5003, 0x65) == 0);
  assert(decoder.h264_waiting_for_idr == 0);
  assert(received_packets == 1);
  assert(decode_single(&decoder, 504, 5004, 0x61) == 0);
  assert(received_packets == 2);

  reset_decoder(&decoder);
  assert(decode_fu(&decoder, 600, 6000, 0x81, first, sizeof(first)) == 0);
  assert(decode_fu(&decoder, 601, 7000, 0x81, first, sizeof(first)) < 0);
  assert(decoder.h264_waiting_for_idr == 1);
  assert(decode_fu(&decoder, 602, 7000, 0x41, last, sizeof(last)) == 0);
  assert(received_packets == 0);
  assert(decode_single(&decoder, 603, 7001, 0x67) == 0);
  assert(received_packets == 1);
  assert(decode_single(&decoder, 604, 7002, 0x61) == 0);
  assert(received_packets == 1);
  assert(decode_fu(&decoder, 605, 7003, 0x85, first, sizeof(first)) == 0);
  assert(decode_fu(&decoder, 606, 7003, 0x45, last, sizeof(last)) == 0);
  assert(decoder.h264_waiting_for_idr == 0);
  assert(received_packets == 2);
  assert(decode_single(&decoder, 607, 7004, 0x61) == 0);
  assert(received_packets == 3);

  puts("H.264 FU-A loss recovery regression: PASS");
  return 0;
}
