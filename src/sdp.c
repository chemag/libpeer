#include <stdarg.h>
#include <stdio.h>

#include "sdp.h"

int sdp_append(char* sdp, const char* format, ...) {
  va_list argptr;

  size_t used = strlen(sdp);
  int written;
  va_start(argptr, format);
  /* Reserve the trailing CRLF and terminator, including for an empty append. */
  if (used >= CONFIG_SDP_BUFFER_SIZE - 2) {
    va_end(argptr);
    return -1;
  }
  written = vsnprintf(sdp + used, CONFIG_SDP_BUFFER_SIZE - used - 2, format, argptr);
  va_end(argptr);
  if (written < 0 || (size_t)written >= CONFIG_SDP_BUFFER_SIZE - used - 2) {
    sdp[used] = '\0';
    return -1;
  }
  used += written;
  if (used > 0 && sdp[used - 1] != '\n') {
    memcpy(sdp + used, "\r\n", 3);
  }
  return 0;
}

void sdp_reset(char* sdp) {
  memset(sdp, 0, CONFIG_SDP_BUFFER_SIZE);
}

void sdp_append_h264(char* sdp) {
  sdp_append(sdp, "m=video 9 UDP/TLS/RTP/SAVPF 96");
  sdp_append(sdp, "c=IN IP4 0.0.0.0");
  sdp_append(sdp, "a=rtcp-fb:96 nack");
  sdp_append(sdp, "a=rtcp-fb:96 nack pli");
  sdp_append(sdp, "a=fmtp:96 profile-level-id=42e01f;level-asymmetry-allowed=1");
  sdp_append(sdp, "a=rtpmap:96 H264/90000");
  sdp_append(sdp, "a=ssrc:1 cname:webrtc-h264");
  sdp_append(sdp, "a=sendrecv");
  sdp_append(sdp, "a=mid:1");
  sdp_append(sdp, "a=rtcp-mux");
}

void sdp_append_vp8(char* sdp) {
  sdp_append(sdp, "m=video 9 UDP/TLS/RTP/SAVPF 97");
  sdp_append(sdp, "c=IN IP4 0.0.0.0");
  sdp_append(sdp, "a=rtcp-fb:97 nack");
  sdp_append(sdp, "a=rtcp-fb:97 nack pli");
  sdp_append(sdp, "a=rtpmap:97 VP8/90000");
  sdp_append(sdp, "a=ssrc:1 cname:webrtc-vp8");
  sdp_append(sdp, "a=sendrecv");
  sdp_append(sdp, "a=mid:1");
  sdp_append(sdp, "a=rtcp-mux");
}

void sdp_append_pcma(char* sdp) {
  sdp_append(sdp, "m=audio 9 UDP/TLS/RTP/SAVP 8");
  sdp_append(sdp, "c=IN IP4 0.0.0.0");
  sdp_append(sdp, "a=rtpmap:8 PCMA/8000");
  sdp_append(sdp, "a=ssrc:4 cname:webrtc-pcma");
  sdp_append(sdp, "a=sendrecv");
  sdp_append(sdp, "a=mid:2");
  sdp_append(sdp, "a=rtcp-mux");
}

void sdp_append_pcmu(char* sdp) {
  sdp_append(sdp, "m=audio 9 UDP/TLS/RTP/SAVP 0");
  sdp_append(sdp, "c=IN IP4 0.0.0.0");
  sdp_append(sdp, "a=rtpmap:0 PCMU/8000");
  sdp_append(sdp, "a=ssrc:5 cname:webrtc-pcmu");
  sdp_append(sdp, "a=sendrecv");
  sdp_append(sdp, "a=mid:2");
  sdp_append(sdp, "a=rtcp-mux");
}

void sdp_append_opus(char* sdp) {
  sdp_append(sdp, "m=audio 9 UDP/TLS/RTP/SAVP 111");
  sdp_append(sdp, "c=IN IP4 0.0.0.0");
  sdp_append(sdp, "a=rtpmap:111 opus/48000/2");
  sdp_append(sdp, "a=ssrc:6 cname:webrtc-opus");
  sdp_append(sdp, "a=sendrecv");
  sdp_append(sdp, "a=mid:2");
  sdp_append(sdp, "a=rtcp-mux");
}

void sdp_append_datachannel(char* sdp) {
  sdp_append(sdp, "m=application 50712 UDP/DTLS/SCTP webrtc-datachannel");
  sdp_append(sdp, "c=IN IP4 0.0.0.0");
  sdp_append(sdp, "a=mid:0");
  sdp_append(sdp, "a=sctp-port:5000");
  sdp_append(sdp, "a=max-message-size:262144");
}

void sdp_create(char* sdp, int b_video, int b_audio, int b_datachannel) {
  char bundle[64];
  sdp_append(sdp, "v=0");
  sdp_append(sdp, "o=- 1495799811084970 1495799811084970 IN IP4 0.0.0.0");
  sdp_append(sdp, "s=-");
  sdp_append(sdp, "t=0 0");
  sdp_append(sdp, "a=msid-semantic: iot");
#if ICE_LITE
  sdp_append(sdp, "a=ice-lite");
#endif
  memset(bundle, 0, sizeof(bundle));

  strcat(bundle, "a=group:BUNDLE");

  if (b_datachannel) {
    strcat(bundle, " 0");
  }

  if (b_video) {
    strcat(bundle, " 1");
  }

  if (b_audio) {
    strcat(bundle, " 2");
  }

  sdp_append(sdp, bundle);
}

static int sdp_has_parameter(const char* fmtp, const char* parameter) {
  size_t wanted = strlen(parameter);
  while (*fmtp) {
    while (*fmtp == ' ' || *fmtp == '\t' || *fmtp == ';') fmtp++;
    const char* end = strchr(fmtp, ';');
    if (!end) end = fmtp + strlen(fmtp);
    const char* trim = end;
    while (trim > fmtp && (trim[-1] == ' ' || trim[-1] == '\t')) trim--;
    if ((size_t)(trim - fmtp) == wanted && strncmp(fmtp, parameter, wanted) == 0) return 1;
    fmtp = *end ? end + 1 : end;
  }
  return 0;
}

int sdp_parse_media(const char* sdp, SdpMedia media[2], MediaCodec video, MediaCodec audio) {
  uint8_t offered[2][128] = {{0}}, mapped[2][128] = {{0}}, compatible[2][128] = {{0}};
  int mux[2] = {0}, count = 0, seen_audio = 0, seen_video = 0;
  char line[256], bundle[80] = "";
  memset(media, 0, 2 * sizeof(*media));
  if (!sdp || strlen(sdp) >= CONFIG_SDP_BUFFER_SIZE) return -1;
  while (*sdp) {
    const char* end = strstr(sdp, "\r\n");
    if (!end || (size_t)(end - sdp) >= sizeof(line)) return -1;
    memcpy(line, sdp, end - sdp);
    line[end - sdp] = '\0';
    sdp = end + 2;
    if (strncmp(line, "m=", 2) == 0) {
      char kind[16], protocol[32];
      int port, offset = 0;
      if (count == 2 || sscanf(line, "m=%15s %d %31s %n", kind, &port, protocol, &offset) != 3 ||
          !offset || port <= 0 || strcmp(protocol, "UDP/TLS/RTP/SAVPF") != 0) return -1;
      SdpMedia* m = &media[count];
      if (strcmp(kind, "video") == 0 && video == CODEC_H264 && !seen_video++) m->video = 1;
      else if (strcmp(kind, "audio") != 0 || audio != CODEC_PCMU || seen_audio++) return -1;
      strcpy(m->direction, "sendrecv");
      const char* p = line + offset;
      while (*p) {
        char* next;
        long pt = strtol(p, &next, 10);
        if (next == p || pt < 0 || pt > 127 || (*next && *next != ' ')) return -1;
        offered[count][pt] = 1;
        p = next;
        while (*p == ' ') p++;
      }
      count++;
    } else if (strncmp(line, "a=group:BUNDLE ", 15) == 0) {
      if (strlen(line + 15) >= sizeof(bundle)) return -1;
      strcpy(bundle, line + 15);
    } else if (count) {
      SdpMedia* m = &media[count - 1];
      int pt, offset = 0;
      char encoding[64];
      if (strncmp(line, "a=mid:", 6) == 0) {
        size_t len = strlen(line + 6);
        if (!len || len >= sizeof(m->mid) || strpbrk(line + 6, " \t")) return -1;
        strcpy(m->mid, line + 6);
      } else if (strcmp(line, "a=rtcp-mux") == 0) {
        mux[count - 1] = 1;
      } else if (strcmp(line, "a=sendonly") == 0) strcpy(m->direction, "recvonly");
      else if (strcmp(line, "a=recvonly") == 0) strcpy(m->direction, "sendonly");
      else if (strcmp(line, "a=inactive") == 0) strcpy(m->direction, "inactive");
      else if (sscanf(line, "a=rtpmap:%d %63s", &pt, encoding) == 2 && pt >= 0 && pt <= 127) {
        mapped[count - 1][pt] = m->video ? strcmp(encoding, "H264/90000") == 0 :
            (pt == 0 && (strcmp(encoding, "PCMU/8000") == 0 || strcmp(encoding, "PCMU/8000/1") == 0));
      } else if (sscanf(line, "a=fmtp:%d %n", &pt, &offset) == 1 && offset && pt >= 0 && pt <= 127) {
        compatible[count - 1][pt] = sdp_has_parameter(line + offset, "packetization-mode=1") &&
            sdp_has_parameter(line + offset, "profile-level-id=42e01f");
      }
    }
  }
  if (!count) return -1;
  /* This profile has one BUNDLE transport, with no unimplemented m-lines. */
  const char* b = bundle;
  for (int i = 0; i < count; i++) {
    char mid[32];
    int offset = 0;
    if (sscanf(b, "%31s %n", mid, &offset) != 1 || !offset || strcmp(mid, media[i].mid) || !mux[i]) return -1;
    b += offset;
    if (i && strcmp(media[0].mid, media[i].mid) == 0) return -1;
    media[i].payload_type = -1;
    for (int pt = 0; pt < 128; pt++) {
      if (offered[i][pt] && (media[i].video ? (mapped[i][pt] && compatible[i][pt]) : pt == 0)) {
        media[i].payload_type = pt;
        break;
      }
    }
    if (media[i].payload_type < 0) return -1;
  }
  if (*b) return -1;
  return count;
}

void sdp_append_media(char* sdp, const SdpMedia* media) {
  int pt = media->payload_type;
  sdp_append(sdp, "m=%s 9 UDP/TLS/RTP/SAVPF %d", media->video ? "video" : "audio", pt);
  sdp_append(sdp, "c=IN IP4 0.0.0.0");
  sdp_append(sdp, "a=mid:%s", media->mid);
  sdp_append(sdp, "a=%s", media->direction);
  sdp_append(sdp, "a=rtcp-mux");
  sdp_append(sdp, "a=rtpmap:%d %s", pt, media->video ? "H264/90000" : "PCMU/8000");
  if (media->video) {
    sdp_append(sdp, "a=fmtp:%d packetization-mode=1;profile-level-id=42e01f;level-asymmetry-allowed=1", pt);
    /* PLI is supported; generic NACK/RTX retransmission is not. */
    sdp_append(sdp, "a=rtcp-fb:%d nack pli", pt);
  }
  sdp_append(sdp, "a=ssrc:%d cname:libpeer", media->video ? 1 : 5);
}
