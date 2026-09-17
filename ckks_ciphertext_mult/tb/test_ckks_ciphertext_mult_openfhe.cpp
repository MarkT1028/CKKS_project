#include "ckks_ciphertext_mult.hpp"

#include "../../common/ckks_ciphertext_mult_vector_file.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

namespace {

static const unsigned int TEST_BUFFER_WORDS =
    CKKS_CT_MULT_RING_DIM * CKKS_CT_MULT_MAX_TOWERS;

static ckks_ct_pair_word_t lhs_words[TEST_BUFFER_WORDS];
static ckks_ct_pair_word_t rhs_words[TEST_BUFFER_WORDS];
static ckks_ct_triple_word_t out_words[TEST_BUFFER_WORDS];
static ckks_rns_param_word_t params[CKKS_CT_MULT_MAX_TOWERS];

unsigned __int128 reciprocal_2pow128(std::uint64_t modulus) {
    const unsigned __int128 maximum =
        ~static_cast<unsigned __int128>(0);
    return maximum / modulus;
}

void load_record(const ckks_ct_mult_vector::Record& record) {
    if (record.metadata.ring_dim != CKKS_CT_MULT_RING_DIM ||
        record.metadata.tower_count == 0 ||
        record.metadata.tower_count > CKKS_CT_MULT_MAX_TOWERS) {
        throw std::runtime_error(
            "OpenFHE vector dimensions are incompatible with the kernel");
    }

    for (unsigned int tower = 0;
         tower < record.metadata.tower_count; ++tower) {
        const auto& tower_parameter = record.towers[tower];
        const unsigned __int128 expected_reciprocal =
            reciprocal_2pow128(tower_parameter.modulus);
        if (tower_parameter.reciprocal_low !=
                static_cast<std::uint64_t>(expected_reciprocal) ||
            tower_parameter.reciprocal_high !=
                static_cast<std::uint64_t>(expected_reciprocal >> 64)) {
            throw std::runtime_error(
                "OpenFHE vector contains an incorrect reciprocal");
        }

        ap_uint<81> reciprocal = 0;
        reciprocal.range(63, 0) = tower_parameter.reciprocal_low;
        reciprocal.range(80, 64) = tower_parameter.reciprocal_high;
        ckks_rns_param_word_t parameter_word = 0;
        parameter_word.range(63, 0) = tower_parameter.modulus;
        parameter_word.range(144, 64) = reciprocal;
        params[tower] = parameter_word;

        const unsigned int base = tower * CKKS_CT_MULT_RING_DIM;
        for (unsigned int coefficient = 0;
             coefficient < CKKS_CT_MULT_RING_DIM; ++coefficient) {
            const unsigned int index = base + coefficient;
            ckks_ct_pair_word_t lhs_word = 0;
            ckks_ct_pair_word_t rhs_word = 0;
            lhs_word.range(63, 0) = record.lhs_c0[index];
            lhs_word.range(127, 64) = record.lhs_c1[index];
            rhs_word.range(63, 0) = record.rhs_c0[index];
            rhs_word.range(127, 64) = record.rhs_c1[index];
            lhs_words[index] = lhs_word;
            rhs_words[index] = rhs_word;
            out_words[index] = 0;
        }
    }
}

int compare_results(const ckks_ct_mult_vector::Record& record) {
    unsigned int errors = 0;
    std::uint64_t checksum = 0;
    for (unsigned int tower = 0;
         tower < record.metadata.tower_count; ++tower) {
        const unsigned int base = tower * CKKS_CT_MULT_RING_DIM;
        for (unsigned int coefficient = 0;
             coefficient < CKKS_CT_MULT_RING_DIM; ++coefficient) {
            const unsigned int index = base + coefficient;
            const std::uint64_t actual_d0 =
                out_words[index].range(63, 0).to_uint64();
            const std::uint64_t actual_d1 =
                out_words[index].range(127, 64).to_uint64();
            const std::uint64_t actual_d2 =
                out_words[index].range(191, 128).to_uint64();
            const std::uint64_t reserved =
                out_words[index].range(255, 192).to_uint64();

            checksum ^= actual_d0 + 0x9E3779B97F4A7C15ULL +
                        (checksum << 6) + (checksum >> 2);
            checksum ^= actual_d1 + 0x9E3779B97F4A7C15ULL +
                        (checksum << 6) + (checksum >> 2);
            checksum ^= actual_d2 + 0x9E3779B97F4A7C15ULL +
                        (checksum << 6) + (checksum >> 2);

            if (actual_d0 != record.expected_d0[index] ||
                actual_d1 != record.expected_d1[index] ||
                actual_d2 != record.expected_d2[index] || reserved != 0) {
                if (errors < 10U) {
                    std::fprintf(
                        stderr,
                        "Mismatch tower=%u coefficient=%u "
                        "expected=(%llu,%llu,%llu) "
                        "actual=(%llu,%llu,%llu) reserved=%llu\n",
                        tower, coefficient,
                        static_cast<unsigned long long>(
                            record.expected_d0[index]),
                        static_cast<unsigned long long>(
                            record.expected_d1[index]),
                        static_cast<unsigned long long>(
                            record.expected_d2[index]),
                        static_cast<unsigned long long>(actual_d0),
                        static_cast<unsigned long long>(actual_d1),
                        static_cast<unsigned long long>(actual_d2),
                        static_cast<unsigned long long>(reserved));
                }
                ++errors;
            }
        }
    }

    if (errors != 0U) {
        std::fprintf(stderr,
                     "FAIL: %u OpenFHE ciphertext words mismatched\n",
                     errors);
        return 1;
    }

    std::printf(
        "PASS: OpenFHE complete ciphertext multiply for %u towers x %u "
        "residues; checksum=0x%016llX\n",
        record.metadata.tower_count,
        record.metadata.ring_dim,
        static_cast<unsigned long long>(checksum));
    return 0;
}

}  // namespace

int main() {
    try {
        const char* vector_path =
            std::getenv("CKKS_CT_MULT_VECTOR_FILE");
        if (vector_path == nullptr || vector_path[0] == '\0') {
            throw std::runtime_error(
                "CKKS_CT_MULT_VECTOR_FILE is not set");
        }

        const ckks_ct_mult_vector::Record record =
            ckks_ct_mult_vector::read_file(vector_path);
        load_record(record);

        std::printf("Loaded OpenFHE complete ciphertext vector: %s\n",
                    vector_path);
        std::printf("  ring_dim=%u towers=%u domain=EVALUATION/NTT\n",
                    record.metadata.ring_dim,
                    record.metadata.tower_count);
        std::printf("  input=(c0,c1)x(c0,c1) expected=(d0,d1,d2)\n");

        const int status = ckks_ciphertext_mult(
            lhs_words, rhs_words, out_words, params,
            record.metadata.ring_dim, record.metadata.tower_count);
        if (status != CKKS_CT_MULT_OK) {
            std::fprintf(stderr, "FAIL: kernel returned status %d\n", status);
            return 1;
        }
        return compare_results(record);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ERROR: %s\n", error.what());
        return 1;
    }
}
