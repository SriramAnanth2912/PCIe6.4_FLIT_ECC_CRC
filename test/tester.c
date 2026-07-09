#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "tester.h"
#include "../src/PCI_SIG_8B_CRC.h"
#include "../src/PCI_SIG_8B_ECC.h"

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/* Encode payload → CRC → ECC, write 256 bytes into ecc_out. */
static void encode(uint8_t *payload, uint8_t ecc_out[ECC_OUT_LEN])
{
    uint8_t crc_out[CRC_OUT_LEN];
    PCI_SIG_8B_CRC(payload, crc_out);
    PCI_SIG_8B_ECC_250_to_256_encoder(crc_out, ecc_out);
}

/* Fill buf with payload_len random bytes. */
static void rand_payload(uint8_t *buf, int payload_len)
{
    for (int i = 0; i < payload_len; i++)
        buf[i] = (uint8_t)(rand() & 0xFF);
}

/*
 * Core test loop.
 *
 * ecc_orig  – clean 256-byte encoded buffer (never mutated)
 * err_idx   – which byte to corrupt (-1 → skip injection, test clean path)
 * xor_val   – XOR mask to apply (-1 → iterate all 256 values)
 *
 * Returns 0 on pass, non-zero on first failure.
 */
static int run_test(uint8_t ecc_orig[ECC_OUT_LEN], int err_idx, int xor_val)
{
    uint8_t ecc[ECC_OUT_LEN];
    uint8_t ecc_decode_out[CRC_OUT_LEN];
    uint8_t crc_decode_out[CRC_OUT_LEN];
    decoder_ctx ctx;

    int j_start = (xor_val < 0) ? 0 : xor_val;
    int j_end = (xor_val < 0) ? 256 : xor_val + 1;

    for (int j = j_start; j < j_end; j++)
    {
        memcpy(ecc, ecc_orig, ECC_OUT_LEN);
        memset(&ctx, 0, sizeof(ctx));

        /* Inject 3-byte burst error when index is in range. */
        if (err_idx >= 0 && err_idx < 254)
        {
            ecc[err_idx] ^= (uint8_t)j;
            ecc[err_idx + 1] ^= (uint8_t)j;
            ecc[err_idx + 2] ^= (uint8_t)j;
        }

        PCI_SIG_8B_ECC_256_to_250_decoder(ecc, ecc_decode_out, &ctx);

        for (int g = 0; g < 3; g++)
        {
            if (ctx.ECC_group[g].unc_error)
            {
                fprintf(stderr, "Uncorrectable ECC error in group %d "
                                "(idx=%d xor=%02X)\n",
                        g, err_idx, j);
                return 2;
            }
        }

        uint8_t crc_rx[8], crc_calc[8];
        memcpy(crc_rx, ecc_decode_out + PAYLOAD_LEN, 8);
        PCI_SIG_8B_CRC(ecc_decode_out, crc_decode_out);
        memcpy(crc_calc, crc_decode_out + PAYLOAD_LEN, 8);

        int cmp = memcmp(crc_calc, crc_rx, 8);
        if (cmp != 0)
        {
            printf("FAIL: idx=%d xor=%02X\n", err_idx, j);
            printf("  CRC received:   ");
            for (int k = 0; k < 8; k++)
                printf("%02X ", crc_rx[k]);
            printf("\n  CRC calculated: ");
            for (int k = 0; k < 8; k++)
                printf("%02X ", crc_calc[k]);
            printf("\n");
            for (int g = 0; g < 3; g++)
            {
                printf("  Group %d: synd_check=%02X synd_parity=%02X "
                       "single=%d unc=%d xor_byte=%d mag=%02X\n",
                       g,
                       ctx.ECC_group[g].synd_check,
                       ctx.ECC_group[g].synd_parity,
                       ctx.ECC_group[g].single_error,
                       ctx.ECC_group[g].unc_error,
                       ctx.ECC_group[g].error_byte,
                       ctx.ECC_group[g].error_magnitude);
            }
            return cmp;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/*
 * check_rand_payload – random payload, sweep all 256 indices × 256 XOR values.
 * Mirrors extensive_testing() but with a freshly generated payload.
 */
int check_rand_payload(int payload_len)
{
    srand((unsigned)time(NULL));
    uint8_t payload[PAYLOAD_LEN];
    rand_payload(payload, payload_len);

    uint8_t ecc_orig[ECC_OUT_LEN];
    encode(payload, ecc_orig);

    for (int i = 0; i < 256; i++)
    {
        int rc = run_test(ecc_orig, i, -1); /* -1 = all XOR values */
        if (rc)
            return rc;
        printf("idx: %d\n", i);
    }
    printf("RAND EXTENSIVE: PASS\n");
    return 0;
}

/*
 * check_payload_idx – fixed payload, single error index, all XOR values.
 */
int check_payload_idx(uint8_t *payload, int idx)
{
    uint8_t ecc_orig[ECC_OUT_LEN];
    encode(payload, ecc_orig);

    int rc = run_test(ecc_orig, idx, -1);
    if (rc == 0)
        printf("idx %d: PASS\n", idx);
    return rc;
}

/*
 * check_rand_payload_idx – random payload, single error index, all XOR values.
 */
int check_rand_payload_idx(int payload_len, int idx)
{
    srand((unsigned)time(NULL));
    uint8_t payload[PAYLOAD_LEN];
    rand_payload(payload, payload_len);

    uint8_t ecc_orig[ECC_OUT_LEN];
    encode(payload, ecc_orig);

    int rc = run_test(ecc_orig, idx, -1);
    if (rc == 0)
        printf("rand idx %d: PASS\n", idx);
    return rc;
}

/*
 * check_payload_idx_with_xor_byte – fixed payload, single index, single XOR byte.
 */
int check_payload_idx_with_xor_byte(uint8_t *payload, int idx, int xor_byte)
{
    uint8_t ecc_orig[ECC_OUT_LEN];
    encode(payload, ecc_orig);

    int rc = run_test(ecc_orig, idx, xor_byte);
    if (rc == 0)
        printf("idx %d xor_byte %02X: PASS\n", idx, xor_byte);
    return rc;
}

/*
 * check_rand_payload_idx_with_xor_byte – random payload, single index, single XOR byte.
 *
 * Note: main.c passes atoi(argv[1]) for idx when argv[1]=="-r", which
 * yields 0.  The signature matches the call site exactly.
 */
int check_rand_payload_idx_with_xor_byte(int payload_len, int idx, int xor_byte)
{
    srand((unsigned)time(NULL));
    uint8_t payload[PAYLOAD_LEN];
    rand_payload(payload, payload_len);

    uint8_t ecc_orig[ECC_OUT_LEN];
    encode(payload, ecc_orig);

    int rc = run_test(ecc_orig, idx, xor_byte);
    if (rc == 0)
        printf("rand idx %d xor_byte %02X: PASS\n", idx, xor_byte);
    return rc;
}

void print_help(void)
{
    printf("Usage: In test folder\n"
           "  ./main                        extensive test on payload_242B.txt\n"
           "  ./main -r                     extensive test on random payload\n"
           "  ./main <idx>                  test single error index, all XOR values\n"
           "  ./main -r <idx>               same with random payload\n"
           "  ./main <idx> <xor_byte>       test single index + single XOR byte\n"
           "  ./main -r <idx> <xor_byte>    same with random payload\n");

    printf("OR \n"
           "  make run                           extensive test on payload_242B.txt\n"
           "  make rand                          extensive test on random payload\n"
           "  make run ARGS=\"<idx>\"              test single error index, all XOR values\n"
           "  make rand ARGS=\"<idx>\"             same with random payload\n"
           "  make run ARGS=\"<idx> <xor_byte>\"   test single index + single XOR byte\n"
           "  make rand ARGS=\"<idx> <xor_byte>\"  same with random payload\n");
}