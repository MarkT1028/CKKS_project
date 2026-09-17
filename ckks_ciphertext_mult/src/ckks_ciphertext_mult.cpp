#include "ckks_ciphertext_mult.hpp"

namespace {

typedef ap_uint<64> residue_t;
typedef ap_uint<60> modulus_residue_t;
typedef ap_uint<81> reciprocal_t;

// Barrett reduction for the public per-tower modulus q.
//
// Preconditions:
//   2^48 <= q < 2^60
//   lhs < q and rhs < q
//   reciprocal = floor(2^128 / q)
//
// Because lhs*rhs < q^2 < 2^120, the approximate quotient is at most one
// below the exact quotient.  Consequently, one conditional subtraction is
// sufficient after forming the remainder.
static residue_t multiply_mod(
    residue_t lhs,
    residue_t rhs,
    modulus_residue_t q,
    reciprocal_t reciprocal)
{
#pragma HLS INLINE
    const modulus_residue_t lhs_narrow = lhs.range(59, 0);
    const modulus_residue_t rhs_narrow = rhs.range(59, 0);
    const ap_uint<120> product =
        (ap_uint<120>)lhs_narrow * (ap_uint<120>)rhs_narrow;

    const ap_uint<201> scaled =
        (ap_uint<201>)product * (ap_uint<201>)reciprocal;
    const ap_uint<64> quotient = scaled.range(191, 128);
    const ap_uint<120> quotient_times_q =
        (ap_uint<120>)quotient * (ap_uint<120>)q;

    ap_uint<121> remainder =
        (ap_uint<121>)product - (ap_uint<121>)quotient_times_q;

    if (remainder >= q) {
        remainder -= q;
    }

    return (residue_t)remainder;
}

static residue_t add_mod(
    residue_t lhs,
    residue_t rhs,
    modulus_residue_t q)
{
#pragma HLS INLINE
    ap_uint<65> sum = (ap_uint<65>)lhs + (ap_uint<65>)rhs;
    if (sum >= q) {
        sum -= q;
    }
    return (residue_t)sum;
}

static bool valid_rns_parameter(
    residue_t q,
    reciprocal_t reciprocal)
{
#pragma HLS INLINE
    const residue_t minimum_q = ((residue_t)1) << 48;
    const residue_t maximum_q = ((residue_t)1) << 60;
    return q >= minimum_q && q < maximum_q && reciprocal != 0;
}

} // namespace

extern "C" int ckks_ciphertext_mult(
    const ckks_ct_pair_word_t *lhs,
    const ckks_ct_pair_word_t *rhs,
    ckks_ct_triple_word_t *out,
    const ckks_rns_param_word_t *rns_params,
    unsigned int ring_dim,
    unsigned int tower_count)
{
#pragma HLS INTERFACE m_axi port=lhs offset=slave bundle=gmem0 depth=131072 \
    max_read_burst_length=64 num_read_outstanding=16
#pragma HLS INTERFACE m_axi port=rhs offset=slave bundle=gmem1 depth=131072 \
    max_read_burst_length=64 num_read_outstanding=16
#pragma HLS INTERFACE m_axi port=out offset=slave bundle=gmem2 depth=131072 \
    max_write_burst_length=64 num_write_outstanding=16
#pragma HLS INTERFACE m_axi port=rns_params offset=slave bundle=gmem3 depth=16 \
    max_read_burst_length=16 num_read_outstanding=4

#pragma HLS INTERFACE s_axilite port=lhs bundle=control
#pragma HLS INTERFACE s_axilite port=rhs bundle=control
#pragma HLS INTERFACE s_axilite port=out bundle=control
#pragma HLS INTERFACE s_axilite port=rns_params bundle=control
#pragma HLS INTERFACE s_axilite port=ring_dim bundle=control
#pragma HLS INTERFACE s_axilite port=tower_count bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    if (ring_dim != CKKS_CT_MULT_RING_DIM) {
        return CKKS_CT_MULT_BAD_RING_DIM;
    }
    if (tower_count == 0U || tower_count > CKKS_CT_MULT_MAX_TOWERS) {
        return CKKS_CT_MULT_BAD_TOWER_COUNT;
    }

    for (unsigned int tower = 0; tower < tower_count; ++tower) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=16
        const ckks_rns_param_word_t param_word = rns_params[tower];
        const residue_t q_wide = param_word.range(63, 0);
        const modulus_residue_t q = q_wide.range(59, 0);
        const reciprocal_t reciprocal = param_word.range(144, 64);

        if (!valid_rns_parameter(q_wide, reciprocal)) {
            return CKKS_CT_MULT_BAD_RNS_PARAMETER;
        }

        const unsigned int tower_base = tower * ring_dim;

        for (unsigned int coefficient = 0;
             coefficient < CKKS_CT_MULT_RING_DIM;
             ++coefficient) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=8192 max=8192
            const unsigned int index = tower_base + coefficient;
            const ckks_ct_pair_word_t lhs_word = lhs[index];
            const ckks_ct_pair_word_t rhs_word = rhs[index];

            const residue_t a0 = lhs_word.range(63, 0);
            const residue_t a1 = lhs_word.range(127, 64);
            const residue_t b0 = rhs_word.range(63, 0);
            const residue_t b1 = rhs_word.range(127, 64);

            const residue_t d0 = multiply_mod(a0, b0, q, reciprocal);
            const residue_t cross0 = multiply_mod(a0, b1, q, reciprocal);
            const residue_t cross1 = multiply_mod(a1, b0, q, reciprocal);
            const residue_t d1 = add_mod(cross0, cross1, q);
            const residue_t d2 = multiply_mod(a1, b1, q, reciprocal);

            ckks_ct_triple_word_t out_word = 0;
            out_word.range(63, 0) = d0;
            out_word.range(127, 64) = d1;
            out_word.range(191, 128) = d2;
            out[index] = out_word;
        }
    }

    return CKKS_CT_MULT_OK;
}
