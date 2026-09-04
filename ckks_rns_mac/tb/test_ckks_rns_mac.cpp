#include "ckks_rns_mac.hpp"

#include "../../common/ckks_rns_vector_file.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kOriginalQ = 36028797018652673ULL;

std::uint64_t compute_barrett_mu(std::uint64_t modulus) {
    const ap_uint<128> numerator =
        static_cast<ap_uint<128>>(1) << 110;
    return static_cast<std::uint64_t>(numerator / modulus);
}

std::uint64_t reference_mac(std::uint64_t a,
                            std::uint64_t b,
                            std::uint64_t acc,
                            std::uint64_t modulus) {
    const ap_uint<128> product =
        static_cast<ap_uint<128>>(a) * static_cast<ap_uint<128>>(b);
    const ap_uint<128> reduced = product % modulus;
    return static_cast<std::uint64_t>((reduced + acc) % modulus);
}

bool is_supported_modulus(std::uint64_t modulus) {
    return modulus >= (1ULL << 54) && modulus < (1ULL << 55);
}

int compare_results(const std::vector<rns_word_t>& out,
                    const std::vector<std::uint64_t>& expected,
                    const std::string& label) {
    int errors = 0;
    std::uint64_t checksum = 0;
    for (int i = 0; i < CKKS_RING_DIM; ++i) {
        const std::uint64_t got =
            static_cast<std::uint64_t>(out[i]);
        checksum ^= got + 0x9e3779b97f4a7c15ULL +
                    (checksum << 6) + (checksum >> 2);
        if (got != expected[i]) {
            if (errors < 10) {
                std::cerr << label << " mismatch i=" << i
                          << " expected=" << expected[i]
                          << " got=" << got << '\n';
            }
            ++errors;
        }
    }
    if (errors != 0) {
        std::cerr << "FAIL " << label << ": " << errors << " / "
                  << CKKS_RING_DIM << " mismatches\n";
        return 1;
    }
    std::cout << "PASS " << label << ": " << CKKS_RING_DIM
              << " modular MAC values; checksum=0x" << std::hex
              << checksum << std::dec << '\n';
    return 0;
}

int run_random_regression(std::uint64_t modulus, std::uint64_t seed) {
    if (!is_supported_modulus(modulus)) {
        throw std::runtime_error(
            "random regression modulus is not 55-bit");
    }
    const std::uint64_t mu = compute_barrett_mu(modulus);
    std::vector<rns_word_t> a(CKKS_RING_DIM);
    std::vector<rns_word_t> b(CKKS_RING_DIM);
    std::vector<rns_word_t> acc(CKKS_RING_DIM);
    std::vector<rns_word_t> out(CKKS_RING_DIM);
    std::vector<std::uint64_t> expected(CKKS_RING_DIM);

    std::mt19937_64 rng(seed);
    for (int i = 0; i < CKKS_RING_DIM; ++i) {
        a[i] = rng() % modulus;
        b[i] = rng() % modulus;
        acc[i] = rng() % modulus;
    }

    const std::uint64_t corners[][3] = {
        {0, 0, 0},
        {1, 1, 0},
        {modulus - 1, modulus - 1, 0},
        {modulus - 1, modulus - 1, modulus - 1},
        {modulus - 1, 1, 1},
        {modulus / 2, modulus / 2, modulus / 2},
    };
    for (unsigned i = 0;
         i < sizeof(corners) / sizeof(corners[0]); ++i) {
        a[i] = corners[i][0];
        b[i] = corners[i][1];
        acc[i] = corners[i][2];
    }
    for (int i = 0; i < CKKS_RING_DIM; ++i) {
        expected[i] = reference_mac(
            static_cast<std::uint64_t>(a[i]),
            static_cast<std::uint64_t>(b[i]),
            static_cast<std::uint64_t>(acc[i]), modulus);
    }

    ckks_rns_mac(a.data(), b.data(), acc.data(), out.data(),
                 modulus, mu);
    return compare_results(
        out, expected, "random q=" + std::to_string(modulus));
}

int run_vector_file(const std::string& path) {
    const ckks_vector::Record record =
        ckks_vector::read_file(path);
    const auto& metadata = record.metadata;
    if (metadata.ring_dim != CKKS_RING_DIM ||
        metadata.limb_bits != CKKS_LIMB_BITS ||
        !is_supported_modulus(metadata.modulus)) {
        throw std::runtime_error(
            "vector metadata is incompatible with HLS kernel");
    }
    if (metadata.barrett_mu !=
        compute_barrett_mu(metadata.modulus)) {
        throw std::runtime_error(
            "vector contains an incorrect Barrett constant");
    }

    std::vector<rns_word_t> a(CKKS_RING_DIM);
    std::vector<rns_word_t> b(CKKS_RING_DIM);
    std::vector<rns_word_t> acc(CKKS_RING_DIM);
    std::vector<rns_word_t> out(CKKS_RING_DIM);
    for (int i = 0; i < CKKS_RING_DIM; ++i) {
        a[i] = record.a[i];
        b[i] = record.b[i];
        acc[i] = record.acc[i];
    }

    ckks_rns_mac(a.data(), b.data(), acc.data(), out.data(),
                 metadata.modulus, metadata.barrett_mu);
    std::cout << "Loaded OpenFHE vector: " << path << '\n'
              << "  tower=" << metadata.tower_index
              << " lhs_component=" << metadata.lhs_component
              << " rhs_component=" << metadata.rhs_component
              << '\n'
              << "  modulus=" << metadata.modulus
              << " barrett_mu=" << metadata.barrett_mu << '\n';
    return compare_results(
        out, record.expected, "OpenFHE ciphertext tower");
}

}  // namespace

int main() {
    try {
        const char* vector_path = std::getenv("CKKS_VECTOR_FILE");
        if (vector_path != nullptr && vector_path[0] != '\0') {
            return run_vector_file(vector_path);
        }

        // Preserve the original deterministic regression, then exercise the
        // new run-time modulus path with a second 55-bit modulus.
        int result =
            run_random_regression(kOriginalQ, 0x0516D1A6ULL);
        result |= run_random_regression(
            18014398510645249ULL, 0x0516D1A6ULL ^ 0x55ULL);
        return result;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
