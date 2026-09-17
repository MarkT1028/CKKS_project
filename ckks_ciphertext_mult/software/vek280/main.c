#include "xparameters.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xiltimer.h"
#include "xckks_ciphertext_mult.h"
#include "generated/openfhe_ciphertext_mult_data.h"

#define CKKS_RING_DIM       CKKS_OPENFHE_RING_DIM
#define TEST_TOWER_COUNT    CKKS_OPENFHE_TOWER_COUNT
#define TEST_WORD_COUNT     CKKS_OPENFHE_WORD_COUNT
#define POLL_LIMIT          100000000U
#define MAX_PRINTED_ERRORS  10U

#if CKKS_OPENFHE_RING_DIM != 8192U
#error "The packaged HLS kernel requires CKKS ring dimension 8192"
#endif

#if CKKS_OPENFHE_TOWER_COUNT == 0U || CKKS_OPENFHE_TOWER_COUNT > 16U
#error "The packaged HLS kernel supports between 1 and 16 RNS towers"
#endif

/*
 * Memory ABI shared with the HLS kernel (AArch64 is little-endian):
 *
 *   input pair:  [127:64] c1, [63:0] c0
 *   output word: [255:192] zero, [191:128] d2,
 *                [127:64] d1, [63:0] d0
 *   RNS params:  [255:145] zero, [144:64] floor(2^128/q), [63:0] q
 *
 * The board only handles RNS ciphertext data and public moduli.  No secret
 * key, plaintext, decryption key, decoding, or decryption is present here.
 */
typedef struct {
    u64 c0;
    u64 c1;
} CkksCiphertextPair;

typedef struct {
    u64 d0;
    u64 d1;
    u64 d2;
    u64 reserved;
} CkksCiphertextTriple;

typedef struct {
    u64 q;
    u64 reciprocal_low;
    u64 reciprocal_high;
    u64 reserved;
} CkksRnsParameter;

/*
 * Global static buffers keep the hardware working set out of the small stack.
 * The generated header contains real OpenFHE ciphertext residues and the
 * corresponding OpenFHE multiplication result.  It contains no plaintext,
 * secret key, decryption key, or decoded value.
 */
static CkksCiphertextPair lhs_words[TEST_WORD_COUNT]
    __attribute__((aligned(64)));
static CkksCiphertextPair rhs_words[TEST_WORD_COUNT]
    __attribute__((aligned(64)));
static CkksCiphertextTriple out_words[TEST_WORD_COUNT]
    __attribute__((aligned(64)));
static CkksRnsParameter rns_params[TEST_TOWER_COUNT]
    __attribute__((aligned(64)));

static void print_u64_hex(u64 value)
{
    xil_printf("%08x%08x",
               (u32)(value >> 32),
               (u32)(value & 0xFFFFFFFFULL));
}

static int validate_openfhe_vectors(void)
{
    u32 tower;
    u32 coefficient;

    for (tower = 0U; tower < TEST_TOWER_COUNT; ++tower) {
        const u64 q = ckks_openfhe_moduli[tower];
        const u32 base = tower * CKKS_RING_DIM;

        if (q < (1ULL << 48) || q >= (1ULL << 60)) {
            xil_printf("FAIL: unsupported modulus in tower %u: 0x",
                       tower);
            print_u64_hex(q);
            xil_printf("\r\n");
            return XST_FAILURE;
        }

        if (ckks_openfhe_reciprocal_low[tower] == 0U &&
            ckks_openfhe_reciprocal_high[tower] == 0U) {
            xil_printf("FAIL: zero Barrett reciprocal in tower %u\r\n",
                       tower);
            return XST_FAILURE;
        }

        for (coefficient = 0U; coefficient < CKKS_RING_DIM;
             ++coefficient) {
            const u32 index = base + coefficient;

            if (ckks_openfhe_lhs_c0[index] >= q ||
                ckks_openfhe_lhs_c1[index] >= q ||
                ckks_openfhe_rhs_c0[index] >= q ||
                ckks_openfhe_rhs_c1[index] >= q ||
                ckks_openfhe_expected_d0[index] >= q ||
                ckks_openfhe_expected_d1[index] >= q ||
                ckks_openfhe_expected_d2[index] >= q) {
                xil_printf("FAIL: non-canonical residue at tower=%u "
                           "coefficient=%u\r\n",
                           tower, coefficient);
                return XST_FAILURE;
            }
        }
    }

    return XST_SUCCESS;
}

static void prepare_test_vectors(void)
{
    u32 tower;
    u32 coefficient;

    for (tower = 0U; tower < TEST_TOWER_COUNT; ++tower) {
        const u32 base = tower * CKKS_RING_DIM;

        rns_params[tower].q = ckks_openfhe_moduli[tower];
        rns_params[tower].reciprocal_low =
            ckks_openfhe_reciprocal_low[tower];
        rns_params[tower].reciprocal_high =
            ckks_openfhe_reciprocal_high[tower];
        rns_params[tower].reserved = 0U;

        for (coefficient = 0U; coefficient < CKKS_RING_DIM;
             ++coefficient) {
            const u32 index = base + coefficient;
            lhs_words[index].c0 = ckks_openfhe_lhs_c0[index];
            lhs_words[index].c1 = ckks_openfhe_lhs_c1[index];
            rhs_words[index].c0 = ckks_openfhe_rhs_c0[index];
            rhs_words[index].c1 = ckks_openfhe_rhs_c1[index];
            out_words[index].d0 = 0U;
            out_words[index].d1 = 0U;
            out_words[index].d2 = 0U;
            out_words[index].reserved = 0U;
        }
    }
}

static u32 verify_results(u64 *checksum_out)
{
    u32 tower;
    u32 coefficient;
    u32 errors = 0U;
    u64 checksum = 0U;

    for (tower = 0U; tower < TEST_TOWER_COUNT; ++tower) {
        const u32 base = tower * CKKS_RING_DIM;

        for (coefficient = 0U; coefficient < CKKS_RING_DIM;
             ++coefficient) {
            const u32 index = base + coefficient;
            const u64 expected_d0 = ckks_openfhe_expected_d0[index];
            const u64 expected_d1 = ckks_openfhe_expected_d1[index];
            const u64 expected_d2 = ckks_openfhe_expected_d2[index];

            checksum ^= out_words[index].d0
                        + 0x9E3779B97F4A7C15ULL
                        + (checksum << 6)
                        + (checksum >> 2);
            checksum ^= out_words[index].d1
                        + 0x9E3779B97F4A7C15ULL
                        + (checksum << 6)
                        + (checksum >> 2);
            checksum ^= out_words[index].d2
                        + 0x9E3779B97F4A7C15ULL
                        + (checksum << 6)
                        + (checksum >> 2);

            if (out_words[index].d0 != expected_d0 ||
                out_words[index].d1 != expected_d1 ||
                out_words[index].d2 != expected_d2 ||
                out_words[index].reserved != 0U) {
                if (errors < MAX_PRINTED_ERRORS) {
                    xil_printf("Mismatch tower=%u coefficient=%u\r\n",
                               tower, coefficient);
                    xil_printf("  expected d0=0x");
                    print_u64_hex(expected_d0);
                    xil_printf(" d1=0x");
                    print_u64_hex(expected_d1);
                    xil_printf(" d2=0x");
                    print_u64_hex(expected_d2);
                    xil_printf("\r\n  actual   d0=0x");
                    print_u64_hex(out_words[index].d0);
                    xil_printf(" d1=0x");
                    print_u64_hex(out_words[index].d1);
                    xil_printf(" d2=0x");
                    print_u64_hex(out_words[index].d2);
                    xil_printf("\r\n");
                }
                ++errors;
            }
        }
    }

    *checksum_out = checksum;
    return errors;
}

int main(void)
{
    XCkks_ciphertext_mult ip;
    XTime start_time;
    XTime end_time;
    u64 elapsed_ticks;
    u64 checksum;
    u32 elapsed_us;
    u32 poll_count;
    u32 errors;
    u32 kernel_status;
    int status;

    xil_printf("\r\n");
    xil_printf("========================================================\r\n");
    xil_printf("VEK280 OpenFHE CKKS ciphertext multiplication test\r\n");
    xil_printf("Input: OpenFHE 1.5.1 encrypted ciphertext residues\r\n");
    xil_printf("No plaintext or secret key is present on the board\r\n");
    xil_printf("========================================================\r\n");

    xil_printf("Vector SHA-256: %s\r\n",
               CKKS_OPENFHE_VECTOR_SHA256);

    if (sizeof(CkksCiphertextPair) != 16U ||
        sizeof(CkksCiphertextTriple) != 32U ||
        sizeof(CkksRnsParameter) != 32U ||
        (sizeof(ckks_openfhe_lhs_c0) /
         sizeof(ckks_openfhe_lhs_c0[0])) != TEST_WORD_COUNT ||
        (sizeof(ckks_openfhe_expected_d2) /
         sizeof(ckks_openfhe_expected_d2[0])) != TEST_WORD_COUNT) {
        xil_printf("FAIL: unexpected compiler structure layout\r\n");
        return XST_FAILURE;
    }

    status = validate_openfhe_vectors();
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    status = XCkks_ciphertext_mult_Initialize(
        &ip,
        (UINTPTR)XPAR_XCKKS_CIPHERTEXT_MULT_0_BASEADDR);
    if (status != XST_SUCCESS) {
        xil_printf("FAIL: IP initialization returned %d\r\n", status);
        return XST_FAILURE;
    }

    xil_printf("IP control base: 0x%08x\r\n",
               (u32)XPAR_XCKKS_CIPHERTEXT_MULT_0_BASEADDR);
    xil_printf("Loading %u towers x %u OpenFHE residues...\r\n",
               TEST_TOWER_COUNT, CKKS_RING_DIM);
    prepare_test_vectors();

    /*
     * CPU writes are initially in its data cache.  The HLS IP is an
     * independent AXI master, so publish every input and clear any dirty
     * output cache lines before starting the accelerator.
     */
    Xil_DCacheFlushRange((UINTPTR)lhs_words, (u32)sizeof(lhs_words));
    Xil_DCacheFlushRange((UINTPTR)rhs_words, (u32)sizeof(rhs_words));
    Xil_DCacheFlushRange((UINTPTR)out_words, (u32)sizeof(out_words));
    Xil_DCacheFlushRange((UINTPTR)rns_params, (u32)sizeof(rns_params));

    XCkks_ciphertext_mult_Set_lhs(&ip, (u64)(UINTPTR)lhs_words);
    XCkks_ciphertext_mult_Set_rhs(&ip, (u64)(UINTPTR)rhs_words);
    XCkks_ciphertext_mult_Set_out_r(&ip, (u64)(UINTPTR)out_words);
    XCkks_ciphertext_mult_Set_rns_params(
        &ip, (u64)(UINTPTR)rns_params);
    XCkks_ciphertext_mult_Set_ring_dim(&ip, CKKS_RING_DIM);
    XCkks_ciphertext_mult_Set_tower_count(&ip, TEST_TOWER_COUNT);

    xil_printf("Starting FPGA ciphertext kernel...\r\n");
    XTime_GetTime(&start_time);
    XCkks_ciphertext_mult_Start(&ip);

    poll_count = 0U;
    while (XCkks_ciphertext_mult_IsDone(&ip) == 0U) {
        ++poll_count;
        if (poll_count >= POLL_LIMIT) {
            xil_printf("FAIL: timeout waiting for HLS IP\r\n");
            return XST_FAILURE;
        }
    }
    XTime_GetTime(&end_time);

    kernel_status = XCkks_ciphertext_mult_Get_return(&ip);
    if (kernel_status != 0U) {
        xil_printf("FAIL: kernel returned status %u\r\n", kernel_status);
        return XST_FAILURE;
    }

    /* FPGA wrote LPDDR directly; discard stale CPU cache lines before read. */
    Xil_DCacheInvalidateRange(
        (UINTPTR)out_words, (u32)sizeof(out_words));

    elapsed_ticks = (u64)(end_time - start_time);
    elapsed_us = (u32)((elapsed_ticks * 1000000ULL) /
                       (u64)COUNTS_PER_SECOND);

    xil_printf("FPGA kernel completed\r\n");
    xil_printf("Timer ticks: %u\r\n", (u32)elapsed_ticks);
    xil_printf("Kernel time: %u us\r\n", elapsed_us);

    errors = verify_results(&checksum);
    if (errors != 0U) {
        xil_printf("FAIL: %u / %u ciphertext words mismatched\r\n",
                   errors, TEST_WORD_COUNT);
        return XST_FAILURE;
    }

    xil_printf("PASS: FPGA output matches OpenFHE "
               "EvalMultNoRelinNoCheck\r\n");
    xil_printf("Validated %u towers x %u residues\r\n",
               TEST_TOWER_COUNT, CKKS_RING_DIM);
    xil_printf("Output components: d0, d1, d2\r\n");
    xil_printf("checksum=0x");
    print_u64_hex(checksum);
    xil_printf("\r\n");

    return XST_SUCCESS;
}
