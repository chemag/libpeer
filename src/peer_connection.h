/**
 * @file peer_connection.h
 * @brief Struct PeerConnection
 */
#ifndef PEER_CONNECTION_H_
#define PEER_CONNECTION_H_

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SdpType {
  SDP_TYPE_OFFER = 0,
  SDP_TYPE_ANSWER,
} SdpType;

typedef enum PeerConnectionState {
  PEER_CONNECTION_NEW,
  PEER_CONNECTION_CHECKING,
  PEER_CONNECTION_CONNECTED,
  PEER_CONNECTION_DISCONNECTED,
  PEER_CONNECTION_FAILED,
  PEER_CONNECTION_CLOSED,
} PeerConnectionState;

typedef enum DataChannelType {

  DATA_CHANNEL_NONE = 0,
  DATA_CHANNEL_STRING,
  DATA_CHANNEL_BINARY,

} DataChannelType;

typedef enum DecpChannelType {
  DATA_CHANNEL_RELIABLE = 0x00,
  DATA_CHANNEL_RELIABLE_UNORDERED = 0x80,
  DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT = 0x01,
  DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT_UNORDERED = 0x81,
  DATA_CHANNEL_PARTIAL_RELIABLE_TIMED = 0x02,
  DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED = 0x82,
} DecpChannelType;

typedef enum MediaCodec {

  CODEC_NONE = 0,

  /* Video */
  CODEC_H264,
  CODEC_VP8,    // not implemented yet
  CODEC_MJPEG,  // not implemented yet

  /* Audio */
  CODEC_OPUS,  // not implemented yet
  CODEC_PCMA,
  CODEC_PCMU,

} MediaCodec;

typedef struct IceServer {
  const char* urls;
  const char* username;
  const char* credential;

} IceServer;

typedef struct PeerConfiguration {
  IceServer ice_servers[5];

  MediaCodec audio_codec;
  MediaCodec video_codec;
  DataChannelType datachannel;

  void (*onaudiotrack)(uint8_t* data, size_t size, void* userdata);
  void (*onvideotrack)(uint8_t* data, size_t size, void* userdata);
  void (*on_request_keyframe)(void* userdata);
  void* user_data;

  /* Optional. Current time in microseconds on the clock that gives the
   * capture_time_us of peer_connection_send_audio_ts() and
   * peer_connection_send_video_ts(). When it is set, a stream sent with these
   * functions also sends RTCP sender reports, which tell the remote peer how
   * its RTP timestamps relate to that clock and so to the other stream, and
   * a stream that is received sends reception reports. */
  uint64_t (*clock_us)(void* user_data);

  /* Optional. When set, they are called in place of onaudiotrack and
   * onvideotrack, with the RTP timestamp of the packet the data came in. The
   * NAL units of one video frame have the same timestamp. */
  void (*onaudiotrack_ts)(uint8_t* data, size_t size, uint32_t rtp_timestamp, void* userdata);
  void (*onvideotrack_ts)(uint8_t* data, size_t size, uint32_t rtp_timestamp, void* userdata);
  /* Optional. Called for each RTCP sender report of the remote audio (video is
   * 0) or video stream: rtp_timestamp is the instant ntp_timestamp, in 32.32
   * NTP format, on the RTP clock of that stream. */
  void (*on_sender_report)(int video, uint64_t ntp_timestamp, uint32_t rtp_timestamp, void* userdata);

} PeerConfiguration;

typedef struct PeerConnection PeerConnection;

const char* peer_connection_state_to_string(PeerConnectionState state);

PeerConnectionState peer_connection_get_state(PeerConnection* pc);

void* peer_connection_get_sctp(PeerConnection* pc);

PeerConnection* peer_connection_create(PeerConfiguration* config);

void peer_connection_destroy(PeerConnection* pc);

void peer_connection_close(PeerConnection* pc);

/**
 * @brief run the connection: connectivity checks, handshake, then reception
 * @param[in] peer connection
 *
 * Once connected it waits up to 1 ms for a datagram, handles the ones that
 * are already received, up to 32 of them, and sends the periodic RTCP reports.
 * The receive callbacks are called from here.
 *
 * Call it, peer_connection_loop_wait(), the peer_connection_send_*() functions
 * and peer_connection_request_keyframe() from one thread: they share the
 * outgoing SRTP session and the state of the streams, without a lock.
 */
int peer_connection_loop(PeerConnection* pc);

/**
 * @brief peer_connection_loop() with the wait of the caller
 * @param[in] peer connection
 * @param[in] longest wait for the first datagram in milliseconds, below 1000;
 *            0 only handles what is already received
 * @return number of datagrams handled
 *
 * The caller can block here in place of sleeping between two calls. The wait
 * also bounds the delay of what the caller sends and of the RTCP reports. In a
 * state without reception, such as new or closed, the call sleeps for the
 * timeout. The connectivity checks and the handshake keep their own waits.
 */
int peer_connection_loop_wait(PeerConnection* pc, int timeout_ms);

int peer_connection_create_datachannel(PeerConnection* pc, DecpChannelType channel_type, uint16_t priority, uint32_t reliability_parameter, char* label, char* protocol);

int peer_connection_create_datachannel_sid(PeerConnection* pc, DecpChannelType channel_type, uint16_t priority, uint32_t reliability_parameter, char* label, char* protocol, uint16_t sid);

/**
 * @brief send message to data channel
 * @param[in] peer connection
 * @param[in] message buffer
 * @param[in] length of message
 */
int peer_connection_datachannel_send(PeerConnection* pc, char* message, size_t len);

int peer_connection_datachannel_send_sid(PeerConnection* pc, char* message, size_t len, uint16_t sid);

int peer_connection_send_audio(PeerConnection* pc, const uint8_t* packet, size_t bytes);

int peer_connection_send_video(PeerConnection* pc, const uint8_t* packet, size_t bytes);

/**
 * @brief send one audio frame with the RTP timestamp of the caller
 * @param[in] peer connection
 * @param[in] encoded frame
 * @param[in] length of the frame
 * @param[in] RTP timestamp of the frame, in units of the codec clock rate
 * @param[in] capture time of the frame in microseconds, on the clock of the application
 *
 * peer_connection_send_audio() counts the RTP timestamp from the number of
 * frames instead. Use one of the two functions for the whole stream, and take
 * the audio and the video capture times from the same clock.
 */
int peer_connection_send_audio_ts(PeerConnection* pc, const uint8_t* buf, size_t len, uint32_t rtp_timestamp, uint64_t capture_time_us);

/**
 * @brief send one video frame with the RTP timestamp of the caller
 * @param[in] peer connection
 * @param[in] one Annex B access unit, which may hold several NAL units
 * @param[in] length of the access unit
 * @param[in] RTP timestamp of the frame, in units of 1/90000 second
 * @param[in] capture time of the frame in microseconds, on the clock of the application
 *
 * Every RTP packet of the access unit carries the timestamp and the last one
 * has the marker bit. peer_connection_send_video() advances the timestamp by
 * 1/30 second per slice instead. Use one of the two functions for the whole stream.
 */
int peer_connection_send_video_ts(PeerConnection* pc, const uint8_t* buf, size_t len, uint32_t rtp_timestamp, uint64_t capture_time_us);

int peer_connection_request_keyframe(PeerConnection* pc);

void peer_connection_set_remote_description(PeerConnection* pc, const char* sdp, SdpType sdp_type);

void peer_connection_set_local_description(PeerConnection* pc, const char* sdp, SdpType sdp_type);

const char* peer_connection_create_offer(PeerConnection* pc);

const char* peer_connection_create_answer(PeerConnection* pc);

/**
 * @brief register callback function to handle packet loss from RTCP receiver report
 * @param[in] peer connection
 * @param[in] callback function void (*cb)(float fraction_loss, uint32_t total_loss, void *userdata)
 * @param[in] userdata for callback function
 */
void peer_connection_on_receiver_packet_loss(PeerConnection* pc,
                                             void (*on_receiver_packet_loss)(float fraction_loss, uint32_t total_loss, void* userdata));

/**
 * @brief Set the callback function to handle onicecandidate event.
 * @param A PeerConnection.
 * @param A callback function to handle onicecandidate event.
 * @param A userdata which is pass to callback function.
 */
void peer_connection_onicecandidate(PeerConnection* pc, void (*onicecandidate)(char* sdp_text, void* userdata));

/**
 * @brief Set the callback function to handle oniceconnectionstatechange event.
 * @param A PeerConnection.
 * @param A callback function to handle oniceconnectionstatechange event.
 * @param A userdata which is pass to callback function.
 */
void peer_connection_oniceconnectionstatechange(PeerConnection* pc,
                                                void (*oniceconnectionstatechange)(PeerConnectionState state, void* userdata));

/**
 * @brief register callback function to handle event of datachannel
 * @param[in] peer connection
 * @param[in] callback function when message received
 * @param[in] callback function when connection is opened
 * @param[in] callback function when connection is closed
 */
void peer_connection_ondatachannel(PeerConnection* pc,
                                   void (*onmessage)(char* msg, size_t len, void* userdata, uint16_t sid),
                                   void (*onopen)(void* userdata),
                                   void (*onclose)(void* userdata));

int peer_connection_lookup_sid(PeerConnection* pc, const char* label, uint16_t* sid);

char* peer_connection_lookup_sid_label(PeerConnection* pc, uint16_t sid);

/**
 * @brief adds a new remote candidate to the peer connection
 * @param[in] peer connection
 * @param[in] ice candidate
 */
int peer_connection_add_ice_candidate(PeerConnection* pc, char* ice_candidate);

#ifdef __cplusplus
}
#endif

#endif  // PEER_CONNECTION_H_
