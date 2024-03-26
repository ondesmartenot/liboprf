#include <string.h>
#include "oprf.h"
#include "toprf.h"

/*
    @copyright 2023, Stefan Marsiske toprf@ctrlc.hu
    This file is part of liboprf.

    liboprf is free software: you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public License
    as published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    liboprf is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the License
    along with liboprf. If not, see <http://www.gnu.org/licenses/>.
*/

// implements TOPRF from https://eprint.iacr.org/2017/363
// quote from page 9 (first line is last on page 8)

// The underlying PRF, fk(x) = H2(x, (H1(x))k), remains unchanged, but the
// key k is shared using Shamir secret-sharing across n servers, where server Si
// stores the key share ki. The initialization of such secret-sharing can be done via
// a Distributed Key Generation (DKG) for discrete-log-based systems, e.g. [16],
// and in Figure 2 we assume it is done with a UC functionality FDKG which we
// discuss further below. For evaluation, given any subset SE of t + 1 servers, the
// user U sends to each of them the same message a = (H′(x))r for random r,
// exactly as in the single-server OPRF protocol 2HashDH. If each server Si in SE
// returned bi = aki then U could reconstruct the value ak using standard Lagrange
// interpolation in the exponent, i.e. ak = � i∈SE bλi i with the Lagrange coefficients
// λi computed using the indexes of servers in SE. After computing ak, the value
// of fk(x) is computed by U by deblinding ak exactly as in the case of protocol
// 2HashDH. Note that this takes a single exponentiation for each server and two
// exponentiations for the user (to compute a and to deblind ak) plus one multi-
// exponentiation by U to compute the Lagrange interpolation on the bi values.

// run with
// gcc -o toprf -g -Wall toprf.c -lsodium liboprf.a

typedef struct {
  uint8_t index;
  uint8_t value[crypto_core_ristretto255_SCALARBYTES];
} __attribute((packed)) TOPRF_Share;

typedef struct {
  uint8_t index;
  uint8_t value[crypto_core_ristretto255_BYTES];
} __attribute((packed)) TOPRF_Part;

void coeff(const uint8_t index, const size_t peers_len, const uint8_t peers[peers_len], uint8_t result[crypto_scalarmult_ristretto255_SCALARBYTES]) {

  uint8_t iscalar[crypto_scalarmult_ristretto255_SCALARBYTES]={0};
  iscalar[0]=index;

  uint8_t divident[crypto_scalarmult_ristretto255_SCALARBYTES]={0};
  divident[0]=1;

  uint8_t divisor[crypto_scalarmult_ristretto255_SCALARBYTES]={0};
  divisor[0]=1;

  for(size_t j=0;j<peers_len;j++) {
    if(peers[j]==index) continue;
    uint8_t tmp[crypto_scalarmult_ristretto255_SCALARBYTES]={0};
    tmp[0]=peers[j];
    //divident*=peers[j];
    crypto_core_ristretto255_scalar_mul(divident, divident, tmp);
    //divisor*=peers[j]-i;
    crypto_core_ristretto255_scalar_sub(tmp, tmp, iscalar);
    crypto_core_ristretto255_scalar_mul(divisor, divisor, tmp);
  }
  crypto_core_ristretto255_scalar_invert(divisor, divisor);
  crypto_core_ristretto255_scalar_mul(result, divisor, divident);
}

void toprf_create_shares(const uint8_t secret[crypto_core_ristretto255_SCALARBYTES],
                   const uint8_t n,
                   const uint8_t threshold,
                   uint8_t _shares[n][TOPRF_Share_BYTES]) {
  TOPRF_Share *shares= (TOPRF_Share*)_shares;

  uint8_t a[threshold-1][crypto_core_ristretto255_SCALARBYTES];
  uint8_t i;
  for(i=0;i<threshold-1;i++) {
    crypto_core_ristretto255_scalar_random(a[i]);
  }
  for(i=1;i<=n;i++) {
    //f(x) = a_0 + a_1*x + a_2*x^2 + a_3*x^3 + ⋯ + a_(k−1)*x^(k−1)
    shares[i-1].index=i;
    uint8_t x[crypto_core_ristretto255_SCALARBYTES]={0};
    x[0]=i;
    memcpy(shares[i-1].value, secret, crypto_core_ristretto255_SCALARBYTES);
    for(int j=0;j<threshold-1;j++) {
      // a_j^j
      uint8_t tmp[crypto_core_ristretto255_SCALARBYTES];
      crypto_core_ristretto255_scalar_mul(tmp, a[j], x);
      for(int exp=0;exp<j;exp++) {
        crypto_core_ristretto255_scalar_mul(tmp, tmp, x);
      }
      crypto_core_ristretto255_scalar_add(shares[i-1].value, shares[i-1].value, tmp);
    }
  }
}

int toprf_thresholdmult(const size_t response_len,
                        const uint8_t _responses[response_len][TOPRF_Part_BYTES],
                        uint8_t result[crypto_scalarmult_ristretto255_BYTES]) {
  const TOPRF_Part *responses=(TOPRF_Part*) _responses;
  uint8_t lpoly[crypto_scalarmult_ristretto255_SCALARBYTES];
  uint8_t gki[crypto_scalarmult_ristretto255_BYTES];
  memset(result,0,crypto_scalarmult_ristretto255_BYTES);

  uint8_t indexes[response_len];
  for(size_t i=0;i<response_len;i++) {
    indexes[i]=responses[i].index;
  }
  for(size_t i=0;i<response_len;i++) {
    coeff(responses[i].index, response_len, indexes, lpoly);

    // betaki = g^{k_i}^{lpoly}
    if(crypto_scalarmult_ristretto255(gki, lpoly, responses[i].value)) {
      return 1;
    }
    crypto_core_ristretto255_add(result,result,gki);
  }
  return 0;
}

int toprf_Evaluate(const uint8_t _k[TOPRF_Share_BYTES],
                   const uint8_t blinded[crypto_core_ristretto255_BYTES],
                   const uint8_t self, const uint8_t *indexes, const uint16_t index_len,
                   uint8_t _Z[TOPRF_Part_BYTES]) {
  uint8_t lpoly[crypto_scalarmult_ristretto255_SCALARBYTES];
  coeff(self, index_len, indexes, lpoly);
  // kl = k * lpoly

  uint8_t kl[crypto_core_ristretto255_SCALARBYTES];
  const TOPRF_Share *k=(TOPRF_Share*) _k;
  crypto_core_ristretto255_scalar_mul(kl, k->value, lpoly);

  TOPRF_Part *Z=(TOPRF_Part*) _Z;
  if(oprf_Evaluate(kl,blinded, Z->value)) return 1;

  return 0;
}

void toprf_thresholdcombine(const size_t response_len,
                            const uint8_t _responses[response_len][TOPRF_Part_BYTES],
                            uint8_t result[crypto_scalarmult_ristretto255_BYTES]) {

  const TOPRF_Part *responses=(TOPRF_Part*) _responses;
  memset(result,0,crypto_scalarmult_ristretto255_BYTES);

  for(size_t i=0;i<response_len;i++) {
    crypto_core_ristretto255_add(result,result,responses[i].value);
  }
}
