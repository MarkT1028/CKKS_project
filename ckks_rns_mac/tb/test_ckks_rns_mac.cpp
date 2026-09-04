#include "ckks_rns_mac.hpp"

#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

namespace {

constexpr std::uint64_t Q = 36028797018652673ULL;

std::uint64_t reference_mac(
    std::uint64_t a, std::uint64_t b, std::uint64_t acc) {
    const ap_uint<128> product =
        static_cast<ap_uint<128>>(a) * static_cast<ap_uint<128>>(b);
    const ap_uint<128> reduced = product % Q;
    return static_cast<std::uint64_t>((reduced + acc) % Q);
}

}  // namespace

int main() {
    std::vector<rns_word_t> a(CKKS_RING_DIM);
    std::vector<rns_word_t> b(CKKS_RING_DIM);
    std::vector<rns_word_t> acc(CKKS_RING_DIM);
    std::vector<rns_word_t> out(CKKS_RING_DIM);

    std::mt19937_64 rng(0x0516D1A6ULL);
    for (int i = 0; i < CKKS_RING_DIM; ++i) {
        a[i] = rng() % Q;
        b[i] = rng() % Q;
        acc[i] = rng() % Q;
    }

    // Explicit corner cases in addition to the deterministic random vectors.
    const std::uint64_t corners[][3] = {
        {0, 0, 0},
        {1, 1, 0},
        {Q - 1, Q - 1, 0},
        {Q - 1, Q - 1, Q - 1},
        {Q - 1, 1, 1},
        {Q / 2, Q / 2, Q / 2},
    };
    for (unsigned i = 0; i < sizeof(corners) / sizeof(corners[0]); ++i) {
        a[i] = corners[i][0];
        b[i] = corners[i][1];
        acc[i] = corners[i][2];
    }

    ckks_rns_mac(a.data(), b.data(), acc.data(), out.data());

    int errors = 0;
    std::uint64_t checksum = 0;
    for (int i = 0; i < CKKS_RING_DIM; ++i) {
        const std::uint64_t av = static_cast<std::uint64_t>(a[i]);
        const std::uint64_t bv = static_cast<std::uint64_t>(b[i]);
        const std::uint64_t cv = static_cast<std::uint64_t>(acc[i]);
        const std::uint64_t got = static_cast<std::uint64_t>(out[i]);
        const std::uint64_t expected = reference_mac(av, bv, cv);
        checksum ^= got + 0x9e3779b97f4a7c15ULL +
                    (checksum << 6) + (checksum >> 2);
        if (got != expected) {
            if (errors < 10) {
                std::cerr << "Mismatch i=" << i
                          << " a=" << av
                          << " b=" << bv
                          << " acc=" << cv
                          << " expected=" << expected
                          << " got=" << got << '\n';
            }
            ++errors;
        }
    }

    if (errors != 0) {
        std::cerr << "FAIL: " << errors << " / " << CKKS_RING_DIM
                  << " mismatches\n";
        return 1;
    }

    std::cout << "PASS: " << CKKS_RING_DIM
              << " bit-exact modular MAC values; checksum=0x"
              << std::hex << checksum << std::dec << '\n';
    return 0;
}
