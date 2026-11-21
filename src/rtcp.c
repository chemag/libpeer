#include <stdio.h>
#include <string.h>

#include "address.h"
#include "rtcp.h"
#include "rtp.h"

int rtcp_probe(uint8_t* packet, size_t size) {
  if (size < 8)
    return -1;

  RtpHeader* header = (RtpHeader*)packet;
  uint8_t type = rtp_header_type(header);
  return ((type >= 64) && (type < 96));
}

int rtcp_get_pli(uint8_t* packet, int len, uint32_t sender_ssrc,
                 uint32_t media_ssrc) {
  if (packet == NULL || len != 12 || sender_ssrc == 0 || media_ssrc == 0)
    return -1;

  memset(packet, 0, len);
  RtcpHeader* rtcp_header = (RtcpHeader*)packet;
  rtcp_header_init(rtcp_header, RTCP_PSFB, 1);
  rtcp_header->length = htons((len / 4) - 1);
  sender_ssrc = htonl(sender_ssrc);
  media_ssrc = htonl(media_ssrc);
  memcpy(packet + 4, &sender_ssrc, sizeof(sender_ssrc));
  memcpy(packet + 8, &media_ssrc, sizeof(media_ssrc));

  return 12;
}

static void rtcp_put32(uint8_t* packet, uint32_t value) {
  value = htonl(value);
  memcpy(packet, &value, sizeof(value));
}

static uint32_t rtcp_get32(const uint8_t* packet) {
  uint32_t value;

  memcpy(&value, packet, sizeof(value));
  return ntohl(value);
}

/* Length of an SDES packet with one CNAME item, 0 if the name is not usable. */
static size_t rtcp_sdes_len(const char* cname) {
  size_t cname_len = cname ? strlen(cname) : 0;

  if (cname_len == 0 || cname_len > 255)
    return 0;
  return (4U + 4U + 2U + cname_len + 1U + 3U) & ~3U;
}

/* The packet must be zeroed: the item list ends with null octets. */
static void rtcp_put_sdes(uint8_t* packet, size_t sdes_len, uint32_t ssrc,
                          const char* cname) {
  RtcpHeader* header = (RtcpHeader*)packet;
  size_t cname_len = strlen(cname);

  rtcp_header_init(header, RTCP_SDES, 1);
  header->length = htons((sdes_len / 4U) - 1U);
  rtcp_put32(packet + 4, ssrc);
  packet[8] = 1;
  packet[9] = (uint8_t)cname_len;
  memcpy(packet + 10, cname, cname_len);
}

uint64_t rtcp_ntp_timestamp(uint64_t time_us) {
  return ((time_us / 1000000U) << 32) | (((time_us % 1000000U) << 32) / 1000000U);
}

int rtcp_get_report(uint8_t* packet, int len, uint32_t ssrc,
                    const RtcpSenderInfo* sender, const RtcpReportBlock* block,
                    const char* cname) {
  size_t report_len = 8U + (sender ? 20U : 0U) + (block ? 24U : 0U);
  size_t sdes_len = rtcp_sdes_len(cname);
  RtcpHeader* header = (RtcpHeader*)packet;
  uint8_t* p;

  if (packet == NULL || ssrc == 0 || sdes_len == 0 ||
      len < 0 || (size_t)len < report_len + sdes_len)
    return -1;
  p = packet + 8;

  memset(packet, 0, report_len + sdes_len);
  rtcp_header_init(header, sender ? RTCP_SR : RTCP_RR, block ? 1 : 0);
  header->length = htons((report_len / 4U) - 1U);
  rtcp_put32(packet + 4, ssrc);
  if (sender) {
    rtcp_put32(p, (uint32_t)(sender->ntp_timestamp >> 32));
    rtcp_put32(p + 4, (uint32_t)sender->ntp_timestamp);
    rtcp_put32(p + 8, sender->rtp_timestamp);
    rtcp_put32(p + 12, sender->packet_count);
    rtcp_put32(p + 16, sender->octet_count);
    p += 20;
  }
  if (block) {
    rtcp_put32(p, block->ssrc);
    rtcp_put32(p + 4, block->flcnpl);
    rtcp_put32(p + 8, block->ehsnr);
    rtcp_put32(p + 12, block->jitter);
    rtcp_put32(p + 16, block->lsr);
    rtcp_put32(p + 20, block->dlsr);
  }
  rtcp_put_sdes(packet + report_len, sdes_len, ssrc, cname);
  return (int)(report_len + sdes_len);
}

int rtcp_parse_sender_report(const uint8_t* packet, size_t size, uint32_t* ssrc,
                             RtcpSenderInfo* sender) {
  if (packet == NULL || size < 28U || packet[1] != RTCP_SR)
    return -1;

  *ssrc = rtcp_get32(packet + 4);
  sender->ntp_timestamp = (uint64_t)rtcp_get32(packet + 8) << 32 | rtcp_get32(packet + 12);
  sender->rtp_timestamp = rtcp_get32(packet + 16);
  sender->packet_count = rtcp_get32(packet + 20);
  sender->octet_count = rtcp_get32(packet + 24);
  return 0;
}

void rtcp_receiver_on_packet(RtcpReceiver* receiver, uint32_t ssrc, uint16_t seq) {
  if (receiver->ssrc != ssrc) {
    /* First packet, or the remote peer started another stream. */
    memset(receiver, 0, sizeof(*receiver));
    receiver->ssrc = ssrc;
    receiver->base_seq = seq;
    receiver->max_seq = seq;
  } else if ((uint16_t)(seq - receiver->max_seq) < 0x8000U) {
    /* In order. A reordered or duplicate packet is only counted. */
    if (seq < receiver->max_seq)
      receiver->cycles += 1U << 16;
    receiver->max_seq = seq;
  }
  receiver->received++;
}

void rtcp_receiver_on_sender_report(RtcpReceiver* receiver, uint64_t ntp_timestamp, uint64_t now_us) {
  receiver->last_sr = (uint32_t)(ntp_timestamp >> 16);
  receiver->last_sr_time_us = now_us;
}

/* RFC 3550, appendix A.3. The interarrival jitter is not measured. */
void rtcp_receiver_get_report_block(RtcpReceiver* receiver, uint64_t now_us, RtcpReportBlock* block) {
  uint32_t extended_max = receiver->cycles + receiver->max_seq;
  uint32_t expected = extended_max - receiver->base_seq + 1U;
  int32_t lost = (int32_t)(expected - receiver->received);
  uint32_t expected_interval = expected - receiver->expected_prior;
  int32_t lost_interval = (int32_t)(expected_interval - (receiver->received - receiver->received_prior));
  uint32_t fraction = 0;

  receiver->expected_prior = expected;
  receiver->received_prior = receiver->received;
  if (expected_interval != 0 && lost_interval > 0)
    fraction = ((uint32_t)lost_interval << 8) / expected_interval;
  /* The cumulative number is a signed 24 bit field: duplicates make it negative. */
  if (lost > 0x7fffff)
    lost = 0x7fffff;
  else if (lost < -0x800000)
    lost = -0x800000;

  block->ssrc = receiver->ssrc;
  block->flcnpl = (fraction << 24) | ((uint32_t)lost & 0xffffffU);
  block->ehsnr = extended_max;
  block->jitter = 0;
  block->lsr = receiver->last_sr;
  /* Delay since that sender report, in units of 1/65536 second. */
  block->dlsr = receiver->last_sr ? (uint32_t)((now_us - receiver->last_sr_time_us) * 65536U / 1000000U) : 0;
}

int rtcp_get_compound_pli(uint8_t* packet, int len, uint32_t sender_ssrc,
                          uint32_t media_ssrc, const char* cname) {
  size_t sdes_len = rtcp_sdes_len(cname);
  size_t pli_offset;
  size_t total_len;
  RtcpHeader* header;

  if (packet == NULL || sdes_len == 0 || sender_ssrc == 0 || media_ssrc == 0)
    return -1;
  pli_offset = 8U + sdes_len;
  total_len = pli_offset + 12U;
  if (len < 0 || (size_t)len < total_len)
    return -1;

  memset(packet, 0, total_len);

  header = (RtcpHeader*)packet;
  rtcp_header_init(header, RTCP_RR, 0);
  header->length = htons(1);
  rtcp_put32(packet + 4, sender_ssrc);
  rtcp_put_sdes(packet + 8, sdes_len, sender_ssrc, cname);

  if (rtcp_get_pli(packet + pli_offset, 12, sender_ssrc, media_ssrc) < 0)
    return -1;
  return (int)total_len;
}

int rtcp_get_fir(uint8_t* packet, int len, int* seqnr) {
  if (packet == NULL || len != 20 || seqnr == NULL)
    return -1;

  memset(packet, 0, len);
  RtcpHeader* rtcp = (RtcpHeader*)packet;
  *seqnr = *seqnr + 1;
  if (*seqnr < 0 || *seqnr >= 256)
    *seqnr = 0;

  rtcp_header_init(rtcp, RTCP_PSFB, 4);
  rtcp->length = htons((len / 4) - 1);
  RtcpFb* rtcp_fb = (RtcpFb*)rtcp;
  RtcpFir* fir = (RtcpFir*)rtcp_fb->fci;
  fir->seqnr = htonl(*seqnr << 24);

  return 20;
}

RtcpRr rtcp_parse_rr(uint8_t* packet) {
  RtcpRr rtcp_rr;
  memcpy(&rtcp_rr.header, packet, sizeof(rtcp_rr.header));
  memcpy(&rtcp_rr.report_block[0], packet + 8, 6 * sizeof(uint32_t));

  return rtcp_rr;
}
