#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define PAYLOAD_LEN 242
#include "../src/PCI_SIG_8B_CRC.h"
#include "../src/PCI_SIG_8B_ECC.h"
#include "read_payload.h"

static int extensive_testing(uint8_t payload[PAYLOAD_LEN])
{
    uint8_t crc_out[PAYLOAD_LEN + 8];
    PCI_SIG_8B_CRC(payload, crc_out);

    uint8_t ecc_out[PAYLOAD_LEN + 8 + 6];
    uint8_t ecc_out_temp[PAYLOAD_LEN + 8 + 6];
    PCI_SIG_8B_ECC_250_to_256_encoder(crc_out, ecc_out);

    memcpy(ecc_out_temp, ecc_out, PAYLOAD_LEN + 8 + 6);
    int err_idx;
    int err_xor_value;
    uint8_t ecc_decode_out[PAYLOAD_LEN + 8];
    decoder_ctx context;
    uint8_t crc_bytes_received[8];
    uint8_t crc_bytes_calculated[8];
    uint8_t crc_decode_out[PAYLOAD_LEN + 8];
    for (int i = 0; i < 256; i++)
    {
        err_idx = i;
        for (int j = 0; j < 256; j++)
        {
            err_xor_value = j;
            ecc_out[err_idx] = ecc_out[err_idx] ^ err_xor_value;
            PCI_SIG_8B_ECC_256_to_250_decoder(ecc_out, ecc_decode_out, &context);
            // Should check before proceeding:
            for (int g = 0; g < 3; g++)
            {
                if (context.ECC_group[g].unc_error)
                {
                    fprintf(stderr, "Uncorrectable ECC error in group %d\n", g);
                    printf("idx: %d, original: %02x, changed: %02x \n", err_idx, ecc_out[err_idx] ^ err_xor_value, ecc_out[err_idx]); // changing payload (TCP, LDDP)
                    return 2;                                                                                                         // distinct error code
                }
            }
            memcpy(crc_bytes_received, ecc_decode_out + PAYLOAD_LEN, 8);
            PCI_SIG_8B_CRC(ecc_decode_out, crc_decode_out);
            memcpy(crc_bytes_calculated, crc_decode_out + PAYLOAD_LEN, 8);

            int result = memcmp(crc_bytes_calculated, crc_bytes_received, 8);
            if (result != 0)
            {
                printf("err_xor_value: %02x\n", err_xor_value);
                printf("idx: %d, original: %02x, changed: %02x \n", err_idx, ecc_out[err_idx] ^ err_xor_value, ecc_out[err_idx]); // changing payload (TCP, LDDP)

                printf("crc_bytes_received: \n");
                for (int i = 0; i < (8); i++)
                {
                    printf("%02X ", crc_bytes_received[i]);
                    if ((i + 1) % 8 == 0)
                        printf("\n");
                }
                printf("\n");
                printf("crc_bytes_calculated: \n");
                for (int i = 0; i < (8); i++)
                {
                    printf("%02X ", crc_bytes_calculated[i]);
                    if ((i + 1) % 8 == 0)
                        printf("\n");
                }
                printf("\n");

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
            }
            memcpy(ecc_out, ecc_out_temp, PAYLOAD_LEN + 8 + 6);
            memset(&context, 0, sizeof(context));
        }
        printf("idx: %d\n", err_idx);
    }
    printf("EXTENSIVE TESTING: PASS\n");
    return 0;
}
int main(int argc, char const *argv[])
{
    (void)argc;
    (void)argv;
    uint8_t payload[PAYLOAD_LEN];
    int bytes = read_payload(payload, "/home/sriramananth/WORK/FILES/COMPANY/PCIe6.4/flit_mode_ecc_crc/test/payload_242B.txt", PAYLOAD_LEN);

    if (bytes < 0)
        return 1;

    printf("Read %d bytes\n", bytes);
    int result = extensive_testing(payload);
    printf("result: %d\n", result);
    return result;
}