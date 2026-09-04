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
// modulus is the selected OpenFHE tower's 55-bit prime. barrett_mu must equal
// floor(2^(2*CKKS_LIMB_BITS) / modulus) = floor(2^110 / modulus). Supplying
// both at run time lets one bitstream process different 55-bit RNS towers.
// This is the arithmetic beneath OpenFHE DCRTPoly multiplication; it is not
// yet a complete EvalMult because component convolution, relinearization, key
// switching, and rescaling are outside this kernel.
extern "C" void ckks_rns_mac(
    const rns_word_t a[CKKS_RING_DIM],
    const rns_word_t b[CKKS_RING_DIM],
    const rns_word_t acc[CKKS_RING_DIM],
    rns_word_t out[CKKS_RING_DIM],
    rns_word_t modulus,
    rns_word_t barrett_mu);

#endif
