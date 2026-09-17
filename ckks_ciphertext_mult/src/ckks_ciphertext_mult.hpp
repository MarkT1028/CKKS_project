#ifndef CKKS_CIPHERTEXT_MULT_HPP
#define CKKS_CIPHERTEXT_MULT_HPP

#include <ap_int.h>

// This accelerator implements the coefficient-wise multiplication stage of
// CKKS EvalMultNoRelin while the DCRT polynomials are in EVALUATION/NTT form.
// It never receives plaintext, a secret key, or a decryption key.

static const unsigned int CKKS_CT_MULT_RING_DIM = 8192U;
static const unsigned int CKKS_CT_MULT_MAX_TOWERS = 16U;

// One input word contains both components of one ciphertext residue:
//   bits  63:0  = c0[tower][coefficient]
//   bits 127:64 = c1[tower][coefficient]
typedef ap_uint<128> ckks_ct_pair_word_t;

// One output word contains the three components produced before
// relinearization:
//   bits  63:0   = d0 = a0*b0 mod q
//   bits 127:64  = d1 = a0*b1 + a1*b0 mod q
//   bits 191:128 = d2 = a1*b1 mod q
//   bits 255:192 = reserved and written as zero
typedef ap_uint<256> ckks_ct_triple_word_t;

// Public parameters for one RNS tower:
//   bits  63:0   = q
//   bits 144:64  = floor(2^128 / q), stored in 81 bits
//   bits 255:145 = reserved and must be zero
// Supported q range: 2^48 <= q < 2^60.
typedef ap_uint<256> ckks_rns_param_word_t;

enum CkksCiphertextMultStatus {
    CKKS_CT_MULT_OK = 0,
    CKKS_CT_MULT_BAD_RING_DIM = 1,
    CKKS_CT_MULT_BAD_TOWER_COUNT = 2,
    CKKS_CT_MULT_BAD_RNS_PARAMETER = 3
};

extern "C" int ckks_ciphertext_mult(
    const ckks_ct_pair_word_t *lhs,
    const ckks_ct_pair_word_t *rhs,
    ckks_ct_triple_word_t *out,
    const ckks_rns_param_word_t *rns_params,
    unsigned int ring_dim,
    unsigned int tower_count);

#endif
