#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>

#include "rtp.h"

#define MAX_PACKETS 16

static struct {
  uint32_t timestamp;
  uint16_t sequence;
  uint8_t marker;
  size_t payload;
} packets[MAX_PACKETS];
static int packet_count;

static void on_packet(uint8_t* packet, size_t bytes, void* user_data) {
  const RtpHeader* header = (const RtpHeader*)packet;

  (void)user_data;
  assert(packet_count < MAX_PACKETS);
  packets[packet_count].timestamp = ntohl(header->timestamp);
  packets[packet_count].sequence = ntohs(header->seq_number);
  packets[packet_count].marker = header->mpt >> 7;
  packets[packet_count].payload = bytes - sizeof(RtpHeader);
  packet_count++;
}

static size_t put_nalu(uint8_t* buf, uint8_t header, size_t size) {
  static const uint8_t start_code[] = {0, 0, 0, 1};

  memcpy(buf, start_code, sizeof(start_code));
  buf[4] = header;
  memset(buf + 5, 0x55, size - 1);
  return sizeof(start_code) + size;
}

int main(void) {
  static RtpEncoder encoder;
  static uint8_t frame[8192];
  size_t size;
  size_t octets = 0;
  int i;

  /* Key frame access unit: SPS, PPS and an IDR slice of three fragments. */
  size = put_nalu(frame, 0x67, 20);
  size += put_nalu(frame + size, 0x68, 8);
  size += put_nalu(frame + size, 0x65, 3000);

  rtp_encoder_init(&encoder, CODEC_H264, on_packet, NULL);
  assert(encoder.clock_rate == 90000);
  assert(rtp_encoder_encode_ts(&encoder, frame, size, 123456) == 0);
  assert(packet_count == 5);
  for (i = 0; i < packet_count; i++) {
    assert(packets[i].timestamp == 123456);
    assert(packets[i].sequence == i);
    assert(packets[i].marker == (i == packet_count - 1));
    octets += packets[i].payload;
  }
  assert(encoder.packet_count == 5);
  assert(encoder.octet_count == octets);

  /* Two slices of one picture: one frame, marker on the second slice. */
  packet_count = 0;
  size = put_nalu(frame, 0x41, 100);
  size += put_nalu(frame + size, 0x41, 100);
  assert(rtp_encoder_encode_ts(&encoder, frame, size, 141456) == 0);
  assert(packet_count == 2);
  assert(packets[0].timestamp == 141456 && packets[1].timestamp == 141456);
  assert(packets[0].marker == 0 && packets[1].marker == 1);
  assert(encoder.packet_count == 7);

  /* Without a timestamp from the caller every slice ends a frame. */
  packet_count = 0;
  assert(rtp_encoder_encode(&encoder, frame, size) == 0);
  assert(packet_count == 2);
  assert(packets[0].marker == 1 && packets[1].marker == 1);
  assert(packets[0].timestamp == 141456 + 3000);
  assert(packets[1].timestamp == 141456 + 6000);

  packet_count = 0;
  size = put_nalu(frame, 0x67, 20);
  size += put_nalu(frame + size, 0x68, 8);
  size += put_nalu(frame + size, 0x65, 3000);
  assert(rtp_encoder_encode(&encoder, frame, size) == 0);
  assert(packet_count == 5);
  for (i = 0; i < packet_count; i++) {
    assert(packets[i].timestamp == 141456 + 9000);
    assert(packets[i].marker == (i == packet_count - 1));
  }

  /* Audio: the timestamp of the caller, then the counter from there. */
  packet_count = 0;
  memset(frame, 0xff, 160);
  rtp_encoder_init(&encoder, CODEC_PCMU, on_packet, NULL);
  assert(encoder.clock_rate == 8000);
  assert(rtp_encoder_encode_ts(&encoder, frame, 160, 1000) == 0);
  assert(rtp_encoder_encode_ts(&encoder, frame, 160, 1320) == 0);
  assert(rtp_encoder_encode(&encoder, frame, 160) == 0);
  assert(packet_count == 3);
  assert(packets[0].timestamp == 1000);
  assert(packets[1].timestamp == 1320);
  assert(packets[2].timestamp == 1480);
  assert(encoder.packet_count == 3 && encoder.octet_count == 480);

  printf("RTP send timestamps: PASS\n");
  return 0;
}
