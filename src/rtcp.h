#ifndef RTCP_H_
#define RTCP_H_

#include <stddef.h>
#include <stdint.h>

typedef enum RtcpType {

  RTCP_FIR = 192,
  RTCP_SR = 200,
  RTCP_RR = 201,
  RTCP_SDES = 202,
  RTCP_BYE = 203,
  RTCP_APP = 204,
  RTCP_RTPFB = 205,
  RTCP_PSFB = 206,
  RTCP_XR = 207,

} RtcpType;

typedef struct RtcpHeader {
  uint8_t vprc; /* Version, Padding, Report count/Feedback message type */
  uint8_t type; /* Packet Type */
  uint16_t length;

} RtcpHeader;

static inline void rtcp_header_init(RtcpHeader* header, uint8_t type, uint8_t rc) {
  header->vprc = 0x80U | (rc & 0x1fU);
  header->type = type;
}

static inline uint8_t rtcp_header_version(const RtcpHeader* header) {
  return (header->vprc >> 6) & 0x03U;
}

static inline uint8_t rtcp_header_padding(const RtcpHeader* header) {
  return (header->vprc >> 5) & 0x01U;
}

static inline uint8_t rtcp_header_rc(const RtcpHeader* header) {
  return header->vprc & 0x1fU;
}

typedef struct RtcpReportBlock {
  uint32_t ssrc;
  uint32_t flcnpl;
  uint32_t ehsnr;
  uint32_t jitter;
  uint32_t lsr;
  uint32_t dlsr;

} RtcpReportBlock;

typedef struct RtcpRr {
  RtcpHeader header;
  uint32_t ssrc;
  RtcpReportBlock report_block[1];

} RtcpRr;

typedef struct RtcpFir {
  uint32_t ssrc;
  uint32_t seqnr;

} RtcpFir;

typedef struct RtcpFb {
  RtcpHeader header;
  uint32_t ssrc;
  uint32_t media;
  char fci[1];

} RtcpFb;

/* Sender information of a sender report, in host byte order. */
typedef struct RtcpSenderInfo {
  uint64_t ntp_timestamp; /* 32.32 fixed point seconds */
  uint32_t rtp_timestamp; /* the same instant on the RTP clock of the stream */
  uint32_t packet_count;
  uint32_t octet_count;
} RtcpSenderInfo;

/* Reception statistics of one remote SSRC, the source of its report blocks. */
typedef struct RtcpReceiver {
  uint32_t ssrc; /* 0 until the first RTP packet */
  uint32_t received;
  uint32_t expected_prior; /* expected and received at the previous report */
  uint32_t received_prior;
  uint32_t cycles; /* sequence number wraps, shifted left by 16 */
  uint16_t base_seq;
  uint16_t max_seq;
  uint32_t last_sr;         /* middle 32 bits of the NTP timestamp of the last SR */
  uint64_t last_sr_time_us; /* its arrival, on the clock of the application */
} RtcpReceiver;

int rtcp_probe(uint8_t* packet, size_t size);

/* Microseconds to the 32.32 fixed point format of NTP timestamps. */
uint64_t rtcp_ntp_timestamp(uint64_t time_us);

/* Write the compound packet SR + SDES(CNAME), or RR + SDES(CNAME) without
 * sender information, and return its length. The optional report block is in
 * host byte order. */
int rtcp_get_report(uint8_t* packet, int len, uint32_t ssrc,
                    const RtcpSenderInfo* sender, const RtcpReportBlock* block,
                    const char* cname);

/* Read the SSRC and the sender information of an SR packet. */
int rtcp_parse_sender_report(const uint8_t* packet, size_t size, uint32_t* ssrc,
                             RtcpSenderInfo* sender);

void rtcp_receiver_on_packet(RtcpReceiver* receiver, uint32_t ssrc, uint16_t seq);

void rtcp_receiver_on_sender_report(RtcpReceiver* receiver, uint64_t ntp_timestamp, uint64_t now_us);

/* Fill the report block of the interval since the previous call, in host byte order. */
void rtcp_receiver_get_report_block(RtcpReceiver* receiver, uint64_t now_us, RtcpReportBlock* block);

int rtcp_get_pli(uint8_t* packet, int len, uint32_t sender_ssrc,
                 uint32_t media_ssrc);

int rtcp_get_compound_pli(uint8_t* packet, int len, uint32_t sender_ssrc,
                          uint32_t media_ssrc, const char* cname);

int rtcp_get_fir(uint8_t* packet, int len, int* seqnr);

RtcpRr rtcp_parse_rr(uint8_t* packet);

#endif  // RTCP_H_
