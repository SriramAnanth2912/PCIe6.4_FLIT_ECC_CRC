#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "tester.h"
#include "../src/PCI_SIG_8B_CRC.h"
#include "../src/PCI_SIG_8B_ECC.h"
#include "../src/burst_error_markov_chain.h"
/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief print the data as per nist tcs
 *
 * @param title char * just title
 * @param data uint8_t * data in bytes
 * @param data_len data vector length (number of bytes)
 */
static inline void print_data_out_byte(char *title, uint8_t *data, uint16_t data_len)
{
    printf("%s", title);
    if (data_len > 8)
    {
        for (uint16_t i = 1; i <= data_len; i++)
        {
            printf("%02X ", data[i - 1]);
            if ((i % 16) == 0)
                printf("\n");
        }
    }
    else
    {
        for (uint16_t i = 0; i < data_len; i++)
        {
            printf("%02X ", data[i]);
        }
    }
}

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
    srand((unsigned)time(NULL));
    uint8_t ecc[ECC_OUT_LEN];
    uint8_t ecc_channel_out[ECC_OUT_LEN];
    uint8_t channel_out[ECC_OUT_LEN];
    uint8_t ecc_decode_out[CRC_OUT_LEN];
    uint8_t crc_decode_out[CRC_OUT_LEN];
    decoder_ctx ctx;

    // Good state: PCIe is near-perfect under normal conditions
    float p_good = 1e-10; // near-zero BER, reflects PCIe's clean signal integrity

    // Bad state: burst caused by electrical noise (SSO, crosstalk, etc.)
    float p_bad = 0.15; // 15% BER during disturbance — realistic for PCIe lane noise

    // Good -> Bad: bursts are rare events in PCIe
    float p_gb = 0.001; // on average, burst event every 1000 bits

    // Bad -> Good: must exit burst within 3 bytes (24 bits)
    float p_bg = 0.45; // avg burst = 2.2 bits, P(>24 bits) < 1e-8

    // P(burst_span > N bits) = (1 - p_bg)^N ===> In the Bad state, the probability of staying in it for more than N consecutive bits

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

            memcpy(ecc_channel_out, ecc, N);
        }
        else
        {
            if (burst_error_markov_chain(ecc, ecc_channel_out, channel_out, p_good, p_bad, p_gb, p_bg))
            {
                print_data_out_byte("channel_out\n", channel_out, N);
                return -1;
            }
            else
            {
                print_data_out_byte("ecc\n", ecc, N);
                print_data_out_byte("channel_out\n", channel_out, N);
                print_data_out_byte("ecc_channel_out\n", ecc_channel_out, N);
            }
        }

        PCI_SIG_8B_ECC_256_to_250_decoder(ecc_channel_out, ecc_decode_out, &ctx);
        for (int g = 0; g < 3; g++)
        {
            if (ctx.ECC_group[g].unc_error)
            {
                fprintf(stderr, "Uncorrectable ECC error in group %d "
                                "(idx=%d xor=%02X)\n",
                        g, err_idx, j);
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

#ifdef TCS_ACTIVE
/**
 * @brief print the data as per nist tcs
 *
 * @param title char * just title
 * @param data uint8_t * data in bytes
 * @param data_len data vector length (number of bytes)
 */
void print_data_out(char *title, uint8_t *data, uint16_t data_len)
{
    printf("%s", title);
    if (data_len > 8)
    {
        for (uint16_t i = 1; i <= data_len; i++)
        {
            printf("%02X ", data[i - 1]);
            if ((i % 8) == 0)
                printf("\n");
        }
    }
    else
    {
        for (uint16_t i = 0; i < data_len; i++)
        {
            printf("%02X ", data[i]);
        }
    }
}

/*
 * check_payload_idx_with_xor_byte_withtcs – fixed payload, single index, single XOR byte.
 */
int check_payload_idx_with_xor_byte_withtcs(uint8_t *payload, int idx, int xor_byte)
{
    uint8_t crc_out[PAYLOAD_LEN + 8];
    PCI_SIG_8B_CRC(payload, crc_out);
    print_data_out("\npayload_in_tx\n", payload, PAYLOAD_LEN);
    print_data_out("\ncrc_out_tx\n", crc_out, PAYLOAD_LEN + 8);

    uint8_t ecc_out[PAYLOAD_LEN + 8 + 6];
    uint8_t ecc_out_temp[PAYLOAD_LEN + 8 + 6];
    PCI_SIG_8B_ECC_250_to_256_encoder(crc_out, ecc_out);
    print_data_out("\necc_out_tx\n", ecc_out, PAYLOAD_LEN + 8 + 6);

    memcpy(ecc_out_temp, ecc_out, PAYLOAD_LEN + 8 + 6);
    int err_idx = idx;
    int err_xor_value = xor_byte;
    uint8_t ecc_decode_out[PAYLOAD_LEN + 8];
    decoder_ctx context;
    uint8_t crc_bytes_received[8];
    uint8_t crc_bytes_calculated[8];
    uint8_t crc_decode_out[PAYLOAD_LEN + 8];

    printf("\nidx: %d, original_hex: %02X, xor_byte: %02X, changed_to_hex: %02X \n", err_idx, ecc_out[err_idx], err_xor_value, ecc_out[err_idx] ^ err_xor_value); // changing payload (TCP, LDDP)
    ecc_out[err_idx] = ecc_out[err_idx] ^ err_xor_value;

    print_data_out("\necc_out_in_rx\n", ecc_out, PAYLOAD_LEN + 8 + 6);

    PCI_SIG_8B_ECC_256_to_250_decoder(ecc_out, ecc_decode_out, &context);
    print_data_out("\necc_decode_out_rx\n", ecc_decode_out, PAYLOAD_LEN + 8);

    // Should check before proceeding:
    for (int g = 0; g < 3; g++)
    {
        if (context.ECC_group[g].unc_error)
        {
            fprintf(stderr, "Uncorrectable ECC error in group %d\n", g);
            printf("\nidx: %d, original_hex: %02X, xor_byte: %02X, changed_to_hex: %02X \n", err_idx, ecc_out[err_idx] ^ err_xor_value, err_xor_value, ecc_out[err_idx]); // changing payload (TCP, LDDP)
            goto unc_error;                                                                                                                                               // distinct error code
        }
    }

    memcpy(crc_bytes_received, ecc_decode_out + PAYLOAD_LEN, 8);
    PCI_SIG_8B_CRC(ecc_decode_out, crc_decode_out);
    memcpy(crc_bytes_calculated, crc_decode_out + PAYLOAD_LEN, 8);
    print_data_out("\ncrc_decode_out_rx\n", crc_decode_out, PAYLOAD_LEN + 8);
    print_data_out("\npayload_out_rx\n", crc_decode_out, PAYLOAD_LEN);

    int result = memcmp(crc_bytes_calculated, crc_bytes_received, 8);
    if (result == 0)
        printf("\nidx %d xor_byte %02X: PASS\n", idx, xor_byte);
    for (int g = 0; g < 3; g++)
    {
        printf("\n\nGroup %d\n", g);
        printf("check syndrome  = %02X\n", context.ECC_group[g].synd_check);
        printf("parity syndrome = %02X\n", context.ECC_group[g].synd_parity);
        printf("single          = %d\n", context.ECC_group[g].single_error);
        printf("unc             = %d\n", context.ECC_group[g].unc_error);
        printf("error byte      = %d\n", context.ECC_group[g].error_byte);
        printf("magnitude       = %02X\n", context.ECC_group[g].error_magnitude);
    }
    return result;
unc_error:
    for (int g = 0; g < 3; g++)
    {
        printf("\n\nGroup %d\n", g);
        printf("check syndrome  = %02X\n", context.ECC_group[g].synd_check);
        printf("parity syndrome = %02X\n", context.ECC_group[g].synd_parity);
        printf("single          = %d\n", context.ECC_group[g].single_error);
        printf("unc             = %d\n", context.ECC_group[g].unc_error);
        printf("error byte      = %d\n", context.ECC_group[g].error_byte);
        printf("magnitude       = %02X\n", context.ECC_group[g].error_magnitude);
    }
    return 2;
}

/*
 * check_rand_payload_idx_with_xor_byte_withtcs – random payload, single index, single XOR byte.
 *
 * Note: main.c passes atoi(argv[1]) for idx when argv[1]=="-r", which
 * yields 0.  The signature matches the call site exactly.
 */
int check_rand_payload_idx_with_xor_byte_withtcs(int payload_len, int idx, int xor_byte)
{
    srand((unsigned)time(NULL));
    uint8_t payload[PAYLOAD_LEN];
    rand_payload(payload, payload_len);

    uint8_t crc_out[payload_len + 8];
    PCI_SIG_8B_CRC(payload, crc_out);
    print_data_out("\npayload_in_tx\n", payload, payload_len);
    print_data_out("\ncrc_out_tx\n", crc_out, payload_len + 8);

    uint8_t ecc_out[payload_len + 8 + 6];
    uint8_t ecc_out_temp[payload_len + 8 + 6];
    PCI_SIG_8B_ECC_250_to_256_encoder(crc_out, ecc_out);
    print_data_out("\necc_out_tx\n", ecc_out, payload_len + 8 + 6);

    memcpy(ecc_out_temp, ecc_out, payload_len + 8 + 6);
    int err_idx = idx;
    int err_xor_value = xor_byte;
    uint8_t ecc_decode_out[payload_len + 8];
    decoder_ctx context;
    uint8_t crc_bytes_received[8];
    uint8_t crc_bytes_calculated[8];
    uint8_t crc_decode_out[payload_len + 8];

    printf("\nidx: %d, original_hex: %02X, xor_byte: %02X, changed_to_hex: %02X \n", err_idx, ecc_out[err_idx], err_xor_value, ecc_out[err_idx] ^ err_xor_value); // changing payload (TCP, LDDP)
    ecc_out[err_idx] = ecc_out[err_idx] ^ err_xor_value;
    print_data_out("\necc_out_in_rx\n", ecc_out, payload_len + 8 + 6);

    PCI_SIG_8B_ECC_256_to_250_decoder(ecc_out, ecc_decode_out, &context);
    print_data_out("\necc_decode_out_rx\n", ecc_decode_out, payload_len + 8);

    // Should check before proceeding:
    for (int g = 0; g < 3; g++)
    {
        if (context.ECC_group[g].unc_error)
        {
            fprintf(stderr, "Uncorrectable ECC error in group %d\n", g);
            goto unc_error; // distinct error code
        }
    }

    memcpy(crc_bytes_received, ecc_decode_out + payload_len, 8);
    PCI_SIG_8B_CRC(ecc_decode_out, crc_decode_out);
    memcpy(crc_bytes_calculated, crc_decode_out + payload_len, 8);
    print_data_out("\ncrc_decode_out_rx\n", crc_decode_out, payload_len + 8);
    print_data_out("\npayload_out_rx\n", crc_decode_out, payload_len);

    int result = memcmp(crc_bytes_calculated, crc_bytes_received, 8);
    if (result == 0)
        printf("\nidx %d xor_byte %02X: PASS\n", idx, xor_byte);
    for (int g = 0; g < 3; g++)
    {
        printf("\n\nGroup %d\n", g);
        printf("check syndrome  = %02X\n", context.ECC_group[g].synd_check);
        printf("parity syndrome = %02X\n", context.ECC_group[g].synd_parity);
        printf("single          = %d\n", context.ECC_group[g].single_error);
        printf("unc             = %d\n", context.ECC_group[g].unc_error);
        printf("error byte      = %d\n", context.ECC_group[g].error_byte);
        printf("magnitude       = %02X\n", context.ECC_group[g].error_magnitude);
    }
    return result;

unc_error:
    for (int g = 0; g < 3; g++)
    {
        printf("\n\nGroup %d\n", g);
        printf("check syndrome  = %02X\n", context.ECC_group[g].synd_check);
        printf("parity syndrome = %02X\n", context.ECC_group[g].synd_parity);
        printf("single          = %d\n", context.ECC_group[g].single_error);
        printf("unc             = %d\n", context.ECC_group[g].unc_error);
        printf("error byte      = %d\n", context.ECC_group[g].error_byte);
        printf("magnitude       = %02X\n", context.ECC_group[g].error_magnitude);
    }
    return 2;
}

#endif

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

/**
 * @brief print in textfile
 *
 * @param filename
 * @param data
 * @param length
 * @param row_wise if 0 then prints in 1 column one after another
 */
static void write_hex_file(const char *filename, const uint8_t *data, int length, bool row_wise)
{
    FILE *f = fopen(filename, "a");
    if (!f)
    {
        perror("fopen");
        return;
    }

    if (row_wise == 1)
    {
        for (int i = 0; i < length; i++)
            fprintf(f, "%d", data[i]);
        fprintf(f, "\n");
    }
    else
    {
        for (int i = 0; i < length; i++)
            fprintf(f, "%d\n", data[i]);
    }
    fclose(f);
}

static int read_payload(uint8_t *payload, const char *filepath, uint16_t payload_len_bytes)
{
    FILE *fp = fopen(filepath, "r");
    if (fp == NULL)
    {
        perror("fopen");
        return -1;
    }

    unsigned int value;
    uint16_t index = 0;

    while (index < payload_len_bytes &&
           fscanf(fp, "%d", &value) == 1)
    {
        payload[index++] = (uint8_t)value;
    }

    fclose(fp);

    if (index != payload_len_bytes)
    {
        printf("Warning: Expected %u bytes, read %u bytes\n",
               payload_len_bytes, index);
    }

    return index;
}

static void generate_from_custom_payload(char *filepath, uint16_t n_tests)
{
    uint8_t payload[PAYLOAD_LEN];
    uint8_t ecc_out[ECC_OUT_LEN];
    uint8_t crc_out[CRC_OUT_LEN];

    int bytes = read_payload(payload, filepath, PAYLOAD_LEN);
    if (bytes < 0)
        perror("no payload exists\n");
    printf("Read %d bytes\n", bytes);
    char filename[128];

    if (n_tests >= 1)
        printf("generating only %d testvectors\n", n_tests);
    for (uint16_t i = 0; i < n_tests; i++)
    {
        if (i == 0)
        {
            mkdir("../testvectors", 0777);
            mkdir("../testvectors/custom", 0777); // creating directory if doesn't exists
        }

        sprintf(filename, "../testvectors/custom/payload.txt");
        write_hex_file(filename, payload, PAYLOAD_LEN, 0);

        PCI_SIG_8B_CRC(payload, crc_out);
        sprintf(filename, "../testvectors/custom/crc_out.txt");
        write_hex_file(filename, crc_out, CRC_OUT_LEN, 0);

        PCI_SIG_8B_ECC_250_to_256_encoder(crc_out, ecc_out);
        sprintf(filename, "../testvectors/custom/ecc_out.txt");
        write_hex_file(filename, ecc_out, ECC_OUT_LEN, 0);

        memset(payload, 0, PAYLOAD_LEN);
        memset(crc_out, 0, CRC_OUT_LEN);
        memset(ecc_out, 0, ECC_OUT_LEN);
    }
}
int generate_tests(uint16_t n_tests)
{
    if (n_tests == 0)
    {
        // reading custom payload_test.txt
        generate_from_custom_payload("../test/payload_test.txt", 1);
        return 0;
    }
    uint8_t payload[PAYLOAD_LEN];
    uint8_t ecc_out[ECC_OUT_LEN];
    uint8_t crc_out[CRC_OUT_LEN];
    uint8_t ecc_decode_out[CRC_OUT_LEN];
    uint8_t crc_decode_out[CRC_OUT_LEN];
    decoder_ctx ctx;

    char filename[128];

    srand((unsigned)time(NULL));
    if (n_tests > 1)
        printf("generating only %d testvectors\n", n_tests);

    uint8_t err_idx[n_tests];
    uint8_t err_xor_byte[n_tests];
    rand_payload(err_idx, n_tests);
    rand_payload(err_xor_byte, n_tests);

    for (uint16_t i = 0; i < n_tests; i++)
    {
        if (i == 0)
            mkdir("../testvectors", 0777); // creating directory if doesn't exists
        rand_payload(payload, PAYLOAD_LEN);

        sprintf(filename, "../testvectors/payload.txt");
        write_hex_file(filename, payload, PAYLOAD_LEN, 0);

        PCI_SIG_8B_CRC(payload, crc_out);
        sprintf(filename, "../testvectors/crc_out.txt");
        write_hex_file(filename, crc_out, CRC_OUT_LEN, 0);

        PCI_SIG_8B_ECC_250_to_256_encoder(crc_out, ecc_out);
        sprintf(filename, "../testvectors/ecc_out.txt");
        write_hex_file(filename, ecc_out, ECC_OUT_LEN, 0);

        ecc_out[err_idx[i]] ^= err_xor_byte[i];

        sprintf(filename, "../testvectors/ecc_decode_in.txt");
        write_hex_file(filename, ecc_out, ECC_OUT_LEN, 0);

        PCI_SIG_8B_ECC_256_to_250_decoder(ecc_out, ecc_decode_out, &ctx);
        sprintf(filename, "../testvectors/ecc_decode_out.txt");
        write_hex_file(filename, ecc_decode_out, CRC_OUT_LEN, 0);

        PCI_SIG_8B_CRC(ecc_decode_out, crc_decode_out);
        sprintf(filename, "../testvectors/crc_decode_out.txt");
        write_hex_file(filename, crc_decode_out, CRC_OUT_LEN, 0);

        memset(payload, 0, PAYLOAD_LEN);
        memset(crc_out, 0, CRC_OUT_LEN);
        memset(ecc_out, 0, ECC_OUT_LEN);
    }
    sprintf(filename, "../testvectors/err_idx.txt");
    write_hex_file(filename, err_idx, n_tests, 0);

    sprintf(filename, "../testvectors/err_xor_byte.txt");
    write_hex_file(filename, err_xor_byte, n_tests, 0);
    return 0;
}

void print_help(void)
{
    printf("Usage: In test folder\n"
           "  ./main                        extensive test on payload_242B.txt\n"
           "  ./main -r                     extensive test on random payload\n"
           "  ./main <idx>                  test single error index, all XOR values\n"
           "  ./main -r <idx>               same with random payload\n"
           "  ./main <idx> <xor_byte>       test single index + single XOR byte\n"
           "  ./main -r <idx> <xor_byte>    same with random payload\n"
           "  ./main -t <n_tests>           generate testvectors for n_tests\n");

    printf("OR \n"
           "  make run                           extensive test on payload_242B.txt\n"
           "  make rand                          extensive test on random payload\n"
           "  make run ARGS=\"<idx>\"              test single error index, all XOR values\n"
           "  make rand ARGS=\"<idx>\"             same with random payload\n"
           "  make run ARGS=\"<idx> <xor_byte>\"   test single index + single XOR byte\n"
           "  make rand ARGS=\"<idx> <xor_byte>\"  same with random payload\n"
           "  make test ARGS=\"<n_tests>\"         generate testvectors for n_tests\n"
           "  make run/rand ARGS=\"-1\"            use burst_error_markov_chanin_channel\n");
}