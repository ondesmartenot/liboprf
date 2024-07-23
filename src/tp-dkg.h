#ifndef tp_dkg_h
#define tp_dkg_h
/**
 * @file tp-dkg.h

  SPDX-FileCopyrightText: 2024, Marsiske Stefan
  SPDX-License-Identifier: LGPL-3.0-or-later

  API for the Trusted Party Distributed Key Generation Protocol

  In this protocol there is two roles, the trusted party (TP) and the
  peers. The trusted party connects to all peers and orchestrates the
  protocol which commuicate only via the TP with each other. This way
  the TP acts also as a broadcast medium which is an essential part of
  all DKG protocols.

  In this protocol the trusted party is - as the name implies -
  trusted, but does not learn the result of the DKG. If the trusted
  party is so trusted that it can learn the result of the DKG, then it
  is much simpler to just randomly generate a secret and then share it
  using Shamir's Secret Sharing.

  The peers only identify themselves towards the TP using long-term
  keys, but use ephemeral keys when communicating with each other,
  this makes them unaware of the identities of the others. However
  peers might be using the ephemeral public keys, or any of the
  generated random values to use as a side-channel to leak their
  identity to the other peers.

  The protocol consists of more than 20 steps, but the API hides this
  and provides a state-engine loop, which any user can call
  iteratively while implementing the networking communication
  themselves. This makes it possible to support different
  communication channels like TCP/IP, Bluetooth, UART, etc. A peer
  needs only to support the medium they use, the TP however must of
  course be able to support all the media that the peers require.

  Both the peers and the TP share a similar API schema:

  (0. msg0 = read()) // only for peers
  1. start_{tp|peer}(state, ...)
  (1.5 send(msg0)) // only for TP
  2. {tp|peer}_set_bufs()
  3. while {tp|peer}_not_done(state):
     - input = allocate_memory( dkg_{tp|peer}_input_size(state) )
     - output = allocate_memory( dkg_{tp|peer}_output_size(state) )
     - input = read()
     - res = {tp|peer}_next_step(state, input, output)
     - if res!=0: fail&abort
     (- dkg_tp_peer_msg(state, output, peer_index, msg) // for TP
     (- msg = output) // for peers
     - send(msg)

  // only for peers
  (4. store share)
  (5. peer_free(state))

 */

#include <stdint.h>
#include <sodium.h>
#include "XK.h"
#include "dkg.h"

#define tpdkg_sessionid_SIZE 32
#define tpdkg_msg0_SIZE ( sizeof(TP_DKG_Message)                                         \
                        + crypto_generichash_BYTES/*dst*/                                \
                        + 2 /*n,t*/                                                      \
                        + crypto_sign_PUBLICKEYBYTES /* tp_sign_pk */                    )
#define noise_xk_handshake3_SIZE 64UL
#define tpdkg_msg8_SIZE (sizeof(TP_DKG_Message) /* header */                             \
                         + noise_xk_handshake3_SIZE /* 4th&final noise handshake */      \
                         + sizeof(TOPRF_Share) /* msg: the noise_xk wrapped share */     \
                         + crypto_secretbox_xchacha20poly1305_MACBYTES /* mac of msg */  \
                         + crypto_auth_hmacsha256_BYTES /* key-committing mac over msg*/ )
#define tpdkg_max_err_SIZE 128

/** @struct TP_DKG_Message
    This is the header for each message sent in this protocol.

    @var TP_DKG_Message::sig This field contains a signature over the
         message header, the message body and the sessionid which is
         normally not included in the message

    @var TP_DKG_Message::msgno This field contains the "type" of this
         message, which is strictly tied to the current step of the
         protocol

    @var TP_DKG_Message::len This field contains the length of the
         complete message including the header.

    @var TP_DKG_Message::from This field contains the id of the
         sender, the TP is 0, otherwise its the index of the peer.

    @var TP_DKG_Message::to This field contains the recipient of the
         message, value 0 represents the TP, value 0xff represents a
         broadcast message, all other values (<=N) are the indexes of
         the peers.

    @var TP_DKG_Message::ts This field contains a timestamp proving
         the freshness of the message, the timestamp is a 64 bit value
         counting seconds since 1970-01-01.

    @var TP_DKG_Message::data This field contains the payload of the
         message.

 */
typedef struct {
  uint8_t sig[crypto_sign_BYTES];
  uint8_t msgno;
  uint32_t len;
  uint8_t from;
  uint8_t to;
  uint64_t ts;
  uint8_t sessionid[tpdkg_sessionid_SIZE];
  uint8_t data[];
} __attribute((packed)) TP_DKG_Message;

/** @struct TP_DKG_PeerState

    This struct contains the state of a peer during the execution of
    the TP DKG protocol.

    Most values of this struct are internal variables and should not
    be used. The following variables are useful and can be used by
    users of this API:

    @var TP_DKG_PeerState:n This field contains the value N,
         specifying the total number of peers participating in this
         protocol.

    @var TP_DKG_PeerState:t This field contains the value T,
         specifying the threshold necessary to use shared secret
         generated by this DKG.

    @var TP_DKG_PeerState:index This field contains the index of the
         peer, it is a value between 1 and and N inclusive.

    @var TP_DKG_PeerState:share This field contains the resulting
         share at the end of the DKG and should most probably be
         persisted for later usage. This is the output of the DKG for
         a peer.
 */
typedef struct {
  int step;
  int prev;
  uint8_t sessionid[tpdkg_sessionid_SIZE];
  uint8_t n;
  uint8_t t;
  uint8_t index;
  uint8_t lt_sk[crypto_sign_SECRETKEYBYTES];
  uint8_t sig_pk[crypto_sign_PUBLICKEYBYTES];
  uint8_t sig_sk[crypto_sign_SECRETKEYBYTES];
  uint8_t noise_pk[crypto_scalarmult_BYTES];
  uint8_t noise_sk[crypto_scalarmult_SCALARBYTES];
  uint8_t tp_sig_pk[crypto_sign_PUBLICKEYBYTES];
  uint64_t last_ts;
  uint64_t ts_epsilon;
  uint8_t (*peer_sig_pks)[][crypto_sign_PUBLICKEYBYTES];
  uint8_t (*peer_noise_pks)[][crypto_scalarmult_BYTES];
  Noise_XK_device_t *dev;
  Noise_XK_session_t *(*noise_outs)[];
  Noise_XK_session_t *(*noise_ins)[];
  uint8_t (*commitments)[][crypto_core_ristretto255_BYTES];
  TOPRF_Share (*shares)[];
  TOPRF_Share (*xshares)[];
  uint16_t complaints_len;
  uint16_t (*complaints)[];
  uint8_t my_complaints_len;
  uint8_t (*my_complaints)[];
  crypto_generichash_state transcript;
  TOPRF_Share share;
} TP_DKG_PeerState;

/** @struct TP_DKG_Cheater

    This struct communicates one detected violation of the protocol.

    @var TP_DKG_Cheater::step This is the step in which the violation occured.

    @var TP_DKG_Cheater::error This is the error code specifying the violation.

    @var TP_DKG_Cheater::peer This specifies which peer caused the violation.

    @var TP_DKG_Cheater::other_peer This optionally specifies which
         peer reported the violation, set to 0xfe if unused.
 */
typedef struct {
  int step;
  int error;
  uint8_t peer;
  uint8_t other_peer;
  int invalid_index;
} TP_DKG_Cheater;

// error codes:
// step 18
//    6; accused revealed a key that was not complained about
//    3; hmac verification failure
//    4; share decryption failure
//    5; invalid share index
//    7; unchecked complaint
//    16 + recv_msg error code - invalid msg 8 (final noise hs + hmac-ed share)
//    32 + recv_msg error code - invalid msg11 - key reveal message
//    127 invalid params for verification from accused
//    128 false complaint
//    129 correct complaint

// recv_msg error codes
// 1 invalid msg len
// 2 unexpected msgno
// 3 from
// 4 to
// 5 expired
// 6 signature fail

/** @struct TP_DKG_TPState

    This struct contains the state of the TP during the execution of
    the TP DKG protocol.

    Most values of this struct are internal variables and should not
    be used. The following variables are useful and can be used by
    users of this API:

    @var TP_DKG_PeerState:n This field contains the value N,
         specifying the total number of peers participating in this
         protocol.

    @var TP_DKG_PeerState:t This field contains the value T,
         specifying the threshold necessary to use shared secret
         generated by this DKG.

    @var TP_DKG_PeerState:cheaters This field contains a list of
         cheaters and protocol violators at the end of a failed
         protocol run.

*/
typedef struct {
  int step;
  int prev;
  uint8_t sessionid[tpdkg_sessionid_SIZE];
  uint8_t n;
  uint8_t t;
  uint8_t sig_pk[crypto_sign_PUBLICKEYBYTES];
  uint8_t sig_sk[crypto_sign_SECRETKEYBYTES];
  uint64_t last_ts;
  uint64_t ts_epsilon;
  uint8_t (*peer_sig_pks)[][crypto_sign_PUBLICKEYBYTES];
  uint8_t (*peer_lt_pks)[][crypto_sign_PUBLICKEYBYTES];
  uint8_t (*commitments)[][crypto_core_ristretto255_BYTES];
  // note this could be optimized by only storing the encrypted share and the hmac
  // and also dropping all items where i==j
  uint8_t (*encrypted_shares)[][tpdkg_msg8_SIZE];
  uint16_t complaints_len;
  uint16_t (*complaints)[];
  size_t cheater_len;
  TP_DKG_Cheater (*cheaters)[];
  size_t cheater_max;
  crypto_generichash_state transcript;
} TP_DKG_TPState;

/*
 * Trusted Party functions
 */

/** Starts a new execution of a TP DKG protocol.

    This function initializes the state of the TP and creates an
    initial message containing the parameters for the peers.

    @param [in] ctx : pointer to a TP_DKG_TPState struct, this struct
                will be initialized by this function.

    @param [in] ts_epsilon: how many seconds a message can be old,
                before it is considered unfresh and is rejected. The
                correct value here is difficult to set, small local
                executions with only 2-out-of-3 setups will work with
                as few as 2-3 seconds, big deployments with
                126-out-of-127 might need up to a few hours...

    @param [in] n: the number of peers participating in this execution.

    @param [in] t: the threshold necessary to use the results of this DKG.

    @param [in] proto_name: an array of bytes used as a domain
           seperation tag (DST). Set it to the name of your application.

    @param [in] proto_name_len: the size of the array proto_name, to
           allow non-zero terminated DSTs.

    @param [out] msg0_len: the size of memory allocated to the msg0 parameter.
           should be exactly tpdkg_msg0_SIZE;

    @param [out] msg0: a message to be sent to all peers to initalize them.
    @return 0 if no errors.
 **/
int tpdkg_start_tp(TP_DKG_TPState *ctx, const uint64_t ts_epsilon,
             const uint8_t n, const uint8_t t,
             const char *proto_name, const size_t proto_name_len,
             const size_t msg0_len, TP_DKG_Message *msg0);

/**
   This function sets all the variable sized buffers in the TP_DKG_PeerState structure.

   A number of buffers are needed in the TP state that depend on the N and T parameters.
   These can be allocated on the stack as follows:

   @param [in] cheater_max: is the number of max cheat attempts to be
          recorded. Normally the maximum is t*t-1. It should be provided as
          (sizeof(cheaters) / sizeof(TP_DKG_Cheater))

   @code
   uint8_t tp_commitments[n*t][crypto_core_ristretto255_BYTES];
   uint16_t tp_complaints[n*n];
   uint8_t encrypted_shares[n*n][tpdkg_msg8_SIZE];
   TP_DKG_Cheater cheaters[t*t - 1];
   uint8_t tp_peers_sig_pks[n][crypto_sign_PUBLICKEYBYTES];
   uint8_t peer_lt_pks[n][crypto_sign_PUBLICKEYBYTES];

   tpdkg_tp_set_bufs(&tp, &tp_commitments, &tp_complaints, &encrypted_shares,
                     &cheaters, sizeof(cheaters) / sizeof(TP_DKG_Cheater),
                     &tp_peers_sig_pks, &peer_lt_pks);
   @endcode

   Important to note that peer_lt_pks should contain the long-term
   signing public-keys of each peer. This array must be populated in
   the correct order before the first call to tpdkg_tp_next().
 */
void tpdkg_tp_set_bufs(TP_DKG_TPState *ctx,
                       uint8_t (*commitments)[][crypto_core_ristretto255_BYTES],
                       uint16_t (*complaints)[],
                       uint8_t (*encrypted_shares)[][tpdkg_msg8_SIZE],
                       TP_DKG_Cheater (*cheaters)[], const size_t cheater_max,
                       uint8_t (*tp_peers_sig_pks)[][crypto_sign_PUBLICKEYBYTES],
                       uint8_t (*peer_lt_pks)[][crypto_sign_PUBLICKEYBYTES]);

/**
   This function calculates the size of the buffer needed to hold all
   outputs from the peers serving as input to the next step of the TP.

   An implementer should allocate a buffer of this size, and
   concatenate all messages from all peers in the order of the peers.

   The allocated buffer is to be passed as an input to the
   tpdkg_pt_next() function, after this the buffer SHOULD be
   deallocated.

   @param [in] ctx: an initialized TP_DKG_TPState struct.
   @return 1 on error, otherwise the size to be allocated (can be 0)
 */
size_t tpdkg_tp_input_size(const TP_DKG_TPState *ctx);

/**
   This function calculates the size of the message from each peer to
   be received by the TP.

   @param [in] ctx: an initialized TP_DKG_TPState struct.
   @param [out] sizes: a array of type size_t with exactly N elements.

   @return 0 on if the sizes differ from peer to peer, otherwise all
           peers will be sending messages of equal size. In the latter
           case all items of the sizes array hold the same valid value.
 */
int tpdkg_tp_input_sizes(const TP_DKG_TPState *ctx, size_t *sizes);

/**
   This function calculates the size of the buffer needed to hold the
   output from the tpdkg_tp_next() function.

   An implementer should allocate a buffer of this size and pass it as
   parameter to tpdkg_tp_next().

   @param [in] ctx: an initialized TP_DKG_TPState struct.
   @return 1 on error, otherwise the size to be allocated (can be 0)
*/
size_t tpdkg_tp_output_size(const TP_DKG_TPState *ctx);

/**
   This function exeutes the next step of the TP DKG protocol for the
   trusted party.

   @param [in] ctx: pointer to a valid TP_DKG_TPState.
   @param [in] input: buffer to the input of the current step.
   @param [in] input_len: size of the input buffer.
   @param [out] output: buffer to the output of the current step.
   @param [in] output_len: size of the output buffer.
   @return 0 if no error

   An example of how to use this in concert with tpdkg_tp_input_size()
   and tpdkg_tp_output_size():

   @code
    uint8_t tp_out[tpdkg_tp_output_size(&tp)];
    uint8_t tp_in[tpdkg_tp_input_size(&tp)];
    recv(socket, tp_in, sizeof(tp_in));
    ret = tpdkg_tp_next(&tp, tp_in, sizeof(tp_in), tp_out, sizeof tp_out);
   @endcode
 */
int tpdkg_tp_next(TP_DKG_TPState *ctx, const uint8_t *input, const size_t input_len, uint8_t *output, const size_t output_len);

/**
   This function "converts" the output of tpdkg_tp_next() into a message for the ith peer.

   The outputs of steps of the protocol are sometimes broadcast
   messages where the output is the same for all peers, but some of
   the outputs are dedicated and unique messages for each peer. This
   function returns a pointer to a message and the size of the message
   to be sent for a particular peer specified as a parameter.


   @param [in] ctx: pointer to a valid TP_DKG_TPState.
   @param [in] base: a pointer to the output of the tpdkg_tp_next() function.
   @param [in] base_size: the size of the output of the tpdkg_tp_next() function.
   @param [in] peer: the index of the peer (starting with 0 for the first)
   @param [out] msg: pointer to a pointer to the message to be sent to the ith peer.
   @param [out] len: pointer to the length of the message to be sent to the ith peer.
   @return 0 if no error

   example how to use this in concert with tpdkg_tp_next():

   @code
    ret = tpdkg_tp_next(&tp, tp_in, sizeof(tp_in), tp_out, sizeof tp_out);
    if(0!=ret) {
      // clean up peers
      for(int i=0;i<n;i++) tpdkg_peer_free(&peers[i]);
      return ret;
    }

    for(int i=0;i<tp.n;i++) {
      const uint8_t *msg;
      size_t len;
      if(0!=tpdkg_tp_peer_msg(&tp, tp_out, sizeof tp_out, i, &msg, &len)) {
        return 1;
      }
      _send(network_buf[i+1], &pkt_len[i+1], msg, len);
    }
    @endcode

 */
int tpdkg_tp_peer_msg(const TP_DKG_TPState *ctx, const uint8_t *base, const size_t base_size, const uint8_t peer, const uint8_t **msg, size_t *len);

/** This function checks if the protocol has finished for the TP or
    more tpdk_tp_next() calls are necessary.

   @return 1 if more steps outstanding
 */
int tpdkg_tp_not_done(const TP_DKG_TPState *tp);

/** This function converts a cheater object to a human readable string.

    @param [in] c: the cheater object.
    @param [out] out: the pointer to the pre-allocated buffer receiving the string
    @param [in] outlen: the size of the pre-allocated buffer
    @return the index of the cheating peer.
 */
uint8_t tpdkg_cheater_msg(const TP_DKG_Cheater *c, char *out, const size_t outlen);

/*
 * Peer functions
 */

/** Starts a new execution of a TP DKG protocol for a peer.

    This function initializes the state of the peer.

    @param [in] ctx : pointer to a TP_DKG_TPState struct, this struct
                will be initialized by this function.

    @param [in] ts_epsilon: how many seconds a message can be old,
                before it is considered unfresh and is rejected. The
                correct value here is difficult to set, small local
                executions with only 2-out-of-3 setups will work with
                as few as 2-3 seconds, big deployments with
                126-out-of-127 might need up to a few hours...

    @param [in] peer_lt_sk: the long-term private signing key of the peer.

    @param [in] t: the msg0 sent from the TP after the TP run tpdkg_tp_start().

    @return 0 if no errors.
 **/
int tpdkg_start_peer(TP_DKG_PeerState *ctx, const uint64_t ts_epsilon,
               const uint8_t peer_lt_sk[crypto_sign_SECRETKEYBYTES],
               const TP_DKG_Message *msg0);

/** This function sets all the variable sized buffers in the TP_DKG_PeerState structure.

  The buffer sizes depend on the N and T parameters to the DKG, if
  they are known in advance, great. If not, they are announced by the
  TP in msg0, which is an input to the tpdkg_peer_start() function,
  after this tpdkg_peer_start() function the peerstate is initialized
  and can be used to find out the N and T parameters.

  If you want you can allocate all the buffers on the stack like this:

  @code
  uint8_t peers_sig_pks[peerstate.n][crypto_sign_PUBLICKEYBYTES];
  uint8_t peers_noise_pks[peerstate.n][crypto_scalarmult_BYTES];
  Noise_XK_session_t *noise_outs[peerstate.n];
  Noise_XK_session_t *noise_ins[peerstate.n];
  TOPRF_Share ishares[peerstate.n];
  TOPRF_Share xshares[peerstate.n];
  uint8_t commitments[peerstate.n *peerstate.t][crypto_core_ristretto255_BYTES];
  uint16_t peer_complaints[peersstate.n*peersstate.n];
  uint8_t peer_my_complaints[peerstate.n];
  @endcode

**/
void tpdkg_peer_set_bufs(TP_DKG_PeerState *ctx,
                         uint8_t (*peers_sig_pks)[][crypto_sign_PUBLICKEYBYTES],
                         uint8_t (*peers_noise_pks)[][crypto_scalarmult_BYTES],
                         Noise_XK_session_t *(*noise_outs)[],
                         Noise_XK_session_t *(*noise_ins)[],
                         TOPRF_Share (*shares)[],
                         TOPRF_Share (*xshares)[],
                         uint8_t (*commitments)[][crypto_core_ristretto255_BYTES],
                         uint16_t (*complaints)[],
                         uint8_t (*my_complaints)[]);


/**
   This function calculates the size of the buffer needed to hold the
   output from the TP serving as input to the next step of the peer.

   An implementer should allocate a buffer of this size.

   The allocated buffer is to be passed as an input to the
   tpdkg_peer_next() function, after this the buffer SHOULD be
   deallocated.

   @param [in] ctx: an initialized TP_DKG_PeerState struct.
   @return 1 on error, otherwise the size to be allocated (can be 0)
 */
size_t tpdkg_peer_input_size(const TP_DKG_PeerState *ctx);

/**
   This function calculates the size of the buffer needed to hold the
   output from the tpdkg_peer_next() function.

   An implementer should allocate a buffer of this size and pass it as
   parameter to tpdkg_peer_next().

   @param [in] ctx: an initialized TP_DKG_PeerState struct.
   @return 1 on error, otherwise the size to be allocated (can be 0)
*/
size_t tpdkg_peer_output_size(const TP_DKG_PeerState *ctx);

/**
   This function exeutes the next step of the TP DKG protocol for a
   peer.

   @param [in] ctx: pointer to a valid TP_DKG_PeerState.
   @param [in] input: buffer to the input of the current step.
   @param [in] input_len: size of the input buffer.
   @param [out] output: buffer to the output of the current step.
   @param [in] output_len: size of the output buffer.
   @return 0 if no error

   An example of how to use this in concert with tpdkg_peer_input_size()
   and tpdkg_peer_output_size() while allocating the buffers on the stack:

   @code
   uint8_t peers_out[tpdkg_peer_output_size(&peers[i])];

   uint8_t peer_in[tpdkg_peer_input_size(&peers[i])];
   recv(socket, peer_in, sizeof(peer_in));
   ret = tpdkg_peer_next(&peer,
                         peer_in, sizeof(peer_in),
                         peers_out, sizeof(peers_out));
   @endcode
 */
int tpdkg_peer_next(TP_DKG_PeerState *ctx, const uint8_t *input, const size_t input_len, uint8_t *output, const size_t output_len);

/**
   This function checks if the protocol has finished for the peer or
   more tpdk_peer_next() calls are necessary.

   @return 1 if more steps outstanding
 */
int tpdkg_peer_not_done(const TP_DKG_PeerState *peer);

/**
   This function MUST be called before a peers state is
   deallocated.

   Unfortunately the underlying (but very cool and formally verified)
   Noise XK implementation does allocate a lot of internal state on
   the heap, and thus this must be freed manually.
 */
void tpdkg_peer_free(TP_DKG_PeerState *ctx);

#endif //tp_dkg_h
