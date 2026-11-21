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

int rtcp_get_sender_report(uint8_t* packet, int len, uint32_t ssrc,
                           const RtcpSenderInfo* sender, const char* cname) {
  const size_t sr_len = 28U;
  size_t sdes_len = rtcp_sdes_len(cname);
  RtcpHeader* header = (RtcpHeader*)packet;

  if (packet == NULL || sender == NULL || ssrc == 0 || sdes_len == 0 ||
      len < 0 || (size_t)len < sr_len + sdes_len)
    return -1;

  memset(packet, 0, sr_len + sdes_len);
  rtcp_header_init(header, RTCP_SR, 0);
  header->length = htons((sr_len / 4U) - 1U);
  rtcp_put32(packet + 4, ssrc);
  rtcp_put32(packet + 8, (uint32_t)(sender->ntp_timestamp >> 32));
  rtcp_put32(packet + 12, (uint32_t)sender->ntp_timestamp);
  rtcp_put32(packet + 16, sender->rtp_timestamp);
  rtcp_put32(packet + 20, sender->packet_count);
  rtcp_put32(packet + 24, sender->octet_count);
  rtcp_put_sdes(packet + sr_len, sdes_len, ssrc, cname);
  return (int)(sr_len + sdes_len);
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
