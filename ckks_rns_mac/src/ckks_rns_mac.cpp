#include "ckks_rns_mac.hpp"

namespace {

// Generic Barrett reduction for x < q^2 and a 55-bit q. The host supplies
// mu=floor(2^110/q). This removes the old dependency on one pseudo-Mersenne
// modulus while keeping division out of the synthesized datapath.
static ap_uint<55> reduce_q(ap_uint<110> x,
                            ap_uint<55> q,
                            ap_uint<56> mu) {
#pragma HLS INLINE
    const ap_uint<56> q1 = x.range(109, 54);
    const ap_uint<112> q2 =
        static_cast<ap_uint<112>>(q1) * mu;
    const ap_uint<56> q3 = q2.range(111, 56);

    const ap_uint<56> r1 = x.range(55, 0);
    const ap_uint<111> q3_times_q =
        static_cast<ap_uint<111>>(q3) * q;
    const ap_uint<56> r2 = q3_times_q.range(55, 0);

    ap_uint<57> r;
    if (r1 >= r2) {
        r = static_cast<ap_uint<57>>(r1 - r2);
    } else {
        r = (static_cast<ap_uint<57>>(1) << 56) +
            static_cast<ap_uint<57>>(r1) - r2;
    }

    // For 2^54 <= q < 2^55, Barrett leaves a small bounded correction.
    // Three guarded subtractions also cover the conservative r < 2^56 bound.
    if (r >= q) {
        r -= q;
    }
    if (r >= q) {
        r -= q;
    }
    if (r >= q) {
        r -= q;
    }
    return r.range(54, 0);
}

static ap_uint<55> add_q(ap_uint<55> x,
                         ap_uint<55> y,
                         ap_uint<55> q) {
#pragma HLS INLINE
    ap_uint<56> sum = static_cast<ap_uint<56>>(x) + y;
    if (sum >= q) {
        sum -= q;
    }
    return sum.range(54, 0);
}

}  // namespace

extern "C" void ckks_rns_mac(
    const rns_word_t a[CKKS_RING_DIM],
    const rns_word_t b[CKKS_RING_DIM],
    const rns_word_t acc[CKKS_RING_DIM],
    rns_word_t out[CKKS_RING_DIM],
    rns_word_t modulus,
    rns_word_t barrett_mu) {

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
#pragma HLS INTERFACE s_axilite port=modulus bundle=control
#pragma HLS INTERFACE s_axilite port=barrett_mu bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    const ap_uint<55> q = modulus.range(54, 0);
    const ap_uint<56> mu = barrett_mu.range(55, 0);

mac_loop:
    for (int i = 0; i < CKKS_RING_DIM; ++i) {
#pragma HLS PIPELINE II=1
        const ap_uint<55> av = a[i].range(54, 0);
        const ap_uint<55> bv = b[i].range(54, 0);
        const ap_uint<55> cv = acc[i].range(54, 0);
        const ap_uint<110> product =
            static_cast<ap_uint<110>>(av) * bv;
        const ap_uint<55> product_mod_q = reduce_q(product, q, mu);
        out[i] = add_q(product_mod_q, cv, q);
    }
}
