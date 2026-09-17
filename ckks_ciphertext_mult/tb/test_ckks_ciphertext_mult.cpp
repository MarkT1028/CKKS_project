#include "ckks_ciphertext_mult.hpp"

#include <cstdio>

namespace {

static const unsigned int TEST_TOWERS = 4U;
// Co-simulation copies the interface's declared maximum depth.  Keep the
// buffers at that maximum even though this regression activates four towers.
// These arrays are testbench memory only; they are not synthesized as BRAM.
static const unsigned int TEST_BUFFER_WORDS =
    CKKS_CT_MULT_RING_DIM * CKKS_CT_MULT_MAX_TOWERS;

static ckks_ct_pair_word_t lhs_words[TEST_BUFFER_WORDS];
static ckks_ct_pair_word_t rhs_words[TEST_BUFFER_WORDS];
static ckks_ct_triple_word_t out_words[TEST_BUFFER_WORDS];
static ckks_rns_param_word_t params[TEST_TOWERS];

static unsigned long long rng_state = 0x0516D1A6ULL;

static unsigned long long next_random()
{
    unsigned long long x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static unsigned long long reference_multiply_mod(
    unsigned long long lhs,
    unsigned long long rhs,
    unsigned long long q)
{
    const unsigned __int128 product =
        (unsigned __int128)lhs * (unsigned __int128)rhs;
    return (unsigned long long)(product % (unsigned __int128)q);
}

static unsigned long long reference_add_mod(
    unsigned long long lhs,
    unsigned long long rhs,
    unsigned long long q)
{
    unsigned long long sum = lhs + rhs;
    if (sum >= q) {
        sum -= q;
    }
    return sum;
}

static void set_rns_parameter(
    unsigned int tower,
    unsigned long long q,
    unsigned long long reciprocal_high,
    unsigned long long reciprocal_low)
{
    ap_uint<81> reciprocal = 0;
    reciprocal.range(63, 0) = reciprocal_low;
    reciprocal.range(80, 64) = reciprocal_high;

    ckks_rns_param_word_t word = 0;
    word.range(63, 0) = q;
    word.range(144, 64) = reciprocal;
    params[tower] = word;
}

static void insert_boundary_cases(
    unsigned int tower,
    unsigned long long q)
{
    const unsigned int base = tower * CKKS_CT_MULT_RING_DIM;

    ckks_ct_pair_word_t lhs_word = 0;
    ckks_ct_pair_word_t rhs_word = 0;
    lhs_word.range(63, 0) = q - 1U;
    lhs_word.range(127, 64) = q - 1U;
    rhs_word.range(63, 0) = q - 1U;
    rhs_word.range(127, 64) = q - 1U;
    lhs_words[base] = lhs_word;
    rhs_words[base] = rhs_word;

    lhs_word = 0;
    rhs_word = 0;
    lhs_word.range(63, 0) = 1U;
    lhs_word.range(127, 64) = q - 1U;
    rhs_word.range(63, 0) = q - 1U;
    rhs_word.range(127, 64) = 1U;
    lhs_words[base + 1U] = lhs_word;
    rhs_words[base + 1U] = rhs_word;
}

} // namespace

int main()
{
    const unsigned long long moduli[TEST_TOWERS] = {
        36028797018652673ULL,
        18014398510645249ULL,
        1152921504606846883ULL,
        281474976710683ULL
    };

    // floor(2^128/q), split into high and low 64-bit pieces.
    set_rns_parameter(0U, moduli[0], 0x200ULL, 0x00000012FFFC0000ULL);
    set_rns_parameter(1U, moduli[1], 0x3FFULL, 0xFFFFFEE3FFF0004EULL);
    set_rns_parameter(2U, moduli[2], 0x10ULL, 0x0000000000005D00ULL);
    set_rns_parameter(3U, moduli[3], 0xFFFFULL, 0xFFFFFFE500000000ULL);

    for (unsigned int tower = 0; tower < TEST_TOWERS; ++tower) {
        const unsigned long long q = moduli[tower];
        const unsigned int base = tower * CKKS_CT_MULT_RING_DIM;

        for (unsigned int coefficient = 0;
             coefficient < CKKS_CT_MULT_RING_DIM;
             ++coefficient) {
            ckks_ct_pair_word_t lhs_word = 0;
            ckks_ct_pair_word_t rhs_word = 0;
            lhs_word.range(63, 0) = next_random() % q;
            lhs_word.range(127, 64) = next_random() % q;
            rhs_word.range(63, 0) = next_random() % q;
            rhs_word.range(127, 64) = next_random() % q;
            lhs_words[base + coefficient] = lhs_word;
            rhs_words[base + coefficient] = rhs_word;
            out_words[base + coefficient] = 0;
        }

        insert_boundary_cases(tower, q);
    }

    const int status = ckks_ciphertext_mult(
        lhs_words,
        rhs_words,
        out_words,
        params,
        CKKS_CT_MULT_RING_DIM,
        TEST_TOWERS);

    if (status != CKKS_CT_MULT_OK) {
        std::printf("FAIL: kernel returned status %d\n", status);
        return 1;
    }

    unsigned int errors = 0U;
    for (unsigned int tower = 0; tower < TEST_TOWERS; ++tower) {
        const unsigned long long q = moduli[tower];
        const unsigned int base = tower * CKKS_CT_MULT_RING_DIM;

        for (unsigned int coefficient = 0;
             coefficient < CKKS_CT_MULT_RING_DIM;
             ++coefficient) {
            const unsigned int index = base + coefficient;
            const unsigned long long a0 =
                lhs_words[index].range(63, 0).to_uint64();
            const unsigned long long a1 =
                lhs_words[index].range(127, 64).to_uint64();
            const unsigned long long b0 =
                rhs_words[index].range(63, 0).to_uint64();
            const unsigned long long b1 =
                rhs_words[index].range(127, 64).to_uint64();

            const unsigned long long expected_d0 =
                reference_multiply_mod(a0, b0, q);
            const unsigned long long expected_d1 = reference_add_mod(
                reference_multiply_mod(a0, b1, q),
                reference_multiply_mod(a1, b0, q),
                q);
            const unsigned long long expected_d2 =
                reference_multiply_mod(a1, b1, q);

            const unsigned long long actual_d0 =
                out_words[index].range(63, 0).to_uint64();
            const unsigned long long actual_d1 =
                out_words[index].range(127, 64).to_uint64();
            const unsigned long long actual_d2 =
                out_words[index].range(191, 128).to_uint64();

            if (actual_d0 != expected_d0 ||
                actual_d1 != expected_d1 ||
                actual_d2 != expected_d2) {
                if (errors < 10U) {
                    std::printf(
                        "Mismatch tower=%u coefficient=%u "
                        "expected=(%llu,%llu,%llu) actual=(%llu,%llu,%llu)\n",
                        tower,
                        coefficient,
                        expected_d0,
                        expected_d1,
                        expected_d2,
                        actual_d0,
                        actual_d1,
                        actual_d2);
                }
                ++errors;
            }
        }
    }

    if (errors != 0U) {
        std::printf("FAIL: %u mismatched ciphertext words\n", errors);
        return 1;
    }

    std::printf(
        "PASS: complete 2x2 CKKS ciphertext multiply for %u towers x %u residues\n",
        TEST_TOWERS,
        CKKS_CT_MULT_RING_DIM);
    return 0;
}
