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

int rtcp_get_compound_pli(uint8_t* packet, int len, uint32_t sender_ssrc,
                          uint32_t media_ssrc, const char* cname) {
  size_t cname_len;
  size_t sdes_len;
  size_t pli_offset;
  size_t total_len;
  uint32_t network_sender_ssrc;
  RtcpHeader* header;

  if (packet == NULL || cname == NULL || sender_ssrc == 0 || media_ssrc == 0)
    return -1;
  cname_len = strlen(cname);
  if (cname_len == 0 || cname_len > 255)
    return -1;
  sdes_len = (4U + 4U + 2U + cname_len + 1U + 3U) & ~3U;
  pli_offset = 8U + sdes_len;
  total_len = pli_offset + 12U;
  if (len < 0 || (size_t)len < total_len)
    return -1;

  memset(packet, 0, total_len);
  network_sender_ssrc = htonl(sender_ssrc);

  header = (RtcpHeader*)packet;
  rtcp_header_init(header, RTCP_RR, 0);
  header->length = htons(1);
  memcpy(packet + 4, &network_sender_ssrc, sizeof(network_sender_ssrc));

  header = (RtcpHeader*)(packet + 8);
  rtcp_header_init(header, RTCP_SDES, 1);
  header->length = htons((sdes_len / 4U) - 1U);
  memcpy(packet + 12, &network_sender_ssrc, sizeof(network_sender_ssrc));
  packet[16] = 1;
  packet[17] = (uint8_t)cname_len;
  memcpy(packet + 18, cname, cname_len);

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
