#include "ckks_rns_mac.hpp"

namespace {

// 55-bit NTT-friendly prime:
//   q = 2^55 - 311295 = 36028797018652673
//   (q - 1) is divisible by 16384 = 2 * CKKS_RING_DIM.
static const ap_uint<64> MOD_Q = 36028797018652673ULL;
static const ap_uint<19> FOLD_C = 311295U;

// Reduce x < q^2 using 2^55 == FOLD_C (mod q). Two folds are sufficient:
//   x = x_lo + 2^55*x_hi
//     == x_lo + FOLD_C*x_hi (mod q)
// The first fold is at most 75 bits; the second is below 2^56.
static ap_uint<55> reduce_q(ap_uint<110> x) {
#pragma HLS INLINE
    const ap_uint<55> lo0 = x.range(54, 0);
    const ap_uint<55> hi0 = x.range(109, 55);
    const ap_uint<75> fold1 =
        static_cast<ap_uint<75>>(lo0) +
        static_cast<ap_uint<75>>(hi0) * FOLD_C;

    const ap_uint<55> lo1 = fold1.range(54, 0);
    const ap_uint<20> hi1 = fold1.range(74, 55);
    ap_uint<56> r =
        static_cast<ap_uint<56>>(lo1) +
        static_cast<ap_uint<56>>(hi1) * FOLD_C;

    // The proven bound is below 2*q; the second guarded subtraction is kept
    // as a defensive invariant and is optimized away if HLS proves it unused.
    if (r >= MOD_Q) {
        r -= MOD_Q;
    }
    if (r >= MOD_Q) {
        r -= MOD_Q;
    }
    return r.range(54, 0);
}

static ap_uint<55> add_q(ap_uint<55> x, ap_uint<55> y) {
#pragma HLS INLINE
    ap_uint<56> sum = static_cast<ap_uint<56>>(x) + y;
    if (sum >= MOD_Q) {
        sum -= MOD_Q;
    }
    return sum.range(54, 0);
}

}  // namespace

extern "C" void ckks_rns_mac(
    const rns_word_t a[CKKS_RING_DIM],
    const rns_word_t b[CKKS_RING_DIM],
    const rns_word_t acc[CKKS_RING_DIM],
    rns_word_t out[CKKS_RING_DIM]) {

#pragma HLS INTERFACE m_axi port=a offset=slave bundle=gmem0 \
    max_read_burst_length=64 num_read_outstanding=16
#pragma HLS INTERFACE m_axi port=b offset=slave bundle=gmem1 \
    max_read_burst_length=64 num_read_outstanding=16
#pragma HLS INTERFACE m_axi port=acc offset=slave bundle=gmem2 \
    max_read_burst_length=64 num_read_outstanding=16
#pragma HLS INTERFACE m_axi port=out offset=slave bundle=gmem3 \
    max_write_burst_length=64 num_write_outstanding=16
#pragma HLS INTERFACE s_axilite port=a bundle=control
#pragma HLS INTERFACE s_axilite port=b bundle=control
#pragma HLS INTERFACE s_axilite port=acc bundle=control
#pragma HLS INTERFACE s_axilite port=out bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

mac_loop:
    for (int i = 0; i < CKKS_RING_DIM; ++i) {
#pragma HLS PIPELINE II=1
        const ap_uint<55> av = a[i].range(54, 0);
        const ap_uint<55> bv = b[i].range(54, 0);
        const ap_uint<55> cv = acc[i].range(54, 0);
        const ap_uint<110> product =
            static_cast<ap_uint<110>>(av) * bv;
        const ap_uint<55> product_mod_q = reduce_q(product);
        out[i] = add_q(product_mod_q, cv);
    }
}
