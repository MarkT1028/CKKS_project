#ifndef CKKS_RNS_MAC_HPP
#define CKKS_RNS_MAC_HPP

#include <ap_int.h>

// Project parameters from stage_B/config.py and ckks_metadata.json.
static constexpr int CKKS_RING_DIM = 8192;
static constexpr int CKKS_LIMB_BITS = 55;

using rns_word_t = ap_uint<64>;

// One evaluation-format RNS tower, point-wise:
//   out[i] = (acc[i] + a[i] * b[i]) mod q
//
// q is a 55-bit NTT-friendly prime (q == 1 mod 2*CKKS_RING_DIM) chosen close
// to 2^55 so reduction can use a small pseudo-Mersenne fold instead of a
// general divider. This is an HLS proof-of-concept for the arithmetic beneath
// OpenFHE DCRTPoly multiplication; it is not yet a complete EvalMult because
// relinearization, key switching, and rescaling are outside this kernel.
extern "C" void ckks_rns_mac(
    const rns_word_t a[CKKS_RING_DIM],
    const rns_word_t b[CKKS_RING_DIM],
    const rns_word_t acc[CKKS_RING_DIM],
    rns_word_t out[CKKS_RING_DIM]);

#endif
