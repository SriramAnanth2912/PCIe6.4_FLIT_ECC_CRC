#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "PCI_SIG_8B_ECC.h"

static void ECC_84_to_86_encoder(uint8_t data_in[84], uint8_t data_out[86])
{
    for (int i = 0; i < 84; i++)
    {
        data_out[i] = data_in[i];
    }
    uint8_t parity = 0x00, check = 0x00;
    for (int i = 0; i < 84; i++)
    {
        parity ^= data_in[i];
        for (int j = 0; j < 8; j++)
        {
            check ^= ((((data_in[i] >> j) & 0x01) ? 0xFF : 0x00) & (alpha_powers[84 + j - i][0]));
        }
    }
    data_out[84] = check;
    data_out[85] = parity;
}

static void ECC_86_to_84_decoder(uint8_t data_in[86], uint8_t data_out[84], ECCG *ctx)
{

    uint8_t encoded_data[86];
    uint16_t error_byte_result = 0;
    ECC_84_to_86_encoder(data_in, encoded_data);

    ctx->synd_check = encoded_data[84] ^ data_in[84];
    ctx->synd_parity = encoded_data[85] ^ data_in[85];
    ctx->no_error = 0x00;
    ctx->single_error = 0x00;
    ctx->unc_error = 0x00;
    ctx->error_byte = error_byte_result & 0x7F;
    ctx->error_magnitude = ctx->synd_parity;

    bool synd_parity_0 = (ctx->synd_parity == 0x00);
    bool synd_check_0 = (ctx->synd_check == 0x00);

    uint8_t synd_parity_map_i = alpha_powers[ctx->synd_parity][1];
    uint8_t synd_check_map_i = alpha_powers[ctx->synd_check][1];

    for (int i = 0; i < 84; i++)
    {
        data_out[i] = data_in[i];
    }

    uint8_t unique_case = 0x00;
    unique_case = (synd_check_0 ? 0x01 : 0x00);
    unique_case = (unique_case << 1) | (synd_parity_0 ? 0x01 : 0x00);

    switch (unique_case)
    {
    case 0x03:
        ctx->no_error = 1;
        break;

    case 0x01:
        ctx->single_error = 1;
        error_byte_result = 85;
        ctx->error_byte = error_byte_result & 0x7F;
        ctx->error_magnitude = ctx->synd_parity;
        break;

    case 0x02:
        ctx->single_error = 1;
        error_byte_result = 84;
        ctx->error_byte = error_byte_result & 0x7F;
        ctx->error_magnitude = ctx->synd_check;
        break;

    case 0x00:
    {
        int diff;

        if (synd_check_map_i < synd_parity_map_i)
            diff = 255 + synd_check_map_i - synd_parity_map_i;
        else
            diff = synd_check_map_i - synd_parity_map_i;

        if (diff < 84)
            error_byte_result = (84 - diff) & 0x1FF;
        else
            error_byte_result = (diff % 84) & 0x1FF;

        ctx->error_byte = error_byte_result & 0x7F;
        // printf("\n\nsynd_parity: %d\n", ctx->synd_parity);
        // printf("wrong byte: %02x\n", data_out[error_byte_result]);
        // printf("check_map_i: %d\n", synd_check_map_i);
        // printf("parity_map_i: %d\n", synd_parity_map_i);
        // printf("error_byte_result:%d \n", error_byte_result);
        // printf("diff:%d \n\n", diff);

        if (error_byte_result >= 84)
        {
            ctx->unc_error = 1;
        }
        else
        {
            ctx->single_error = 1;
            data_out[error_byte_result] ^= (ctx->synd_parity);
        }
        break;
    }

    default:
        fprintf(stderr, "Invalid case: 0x%02X\n", unique_case);
        exit(EXIT_FAILURE);
    }
}

void PCI_SIG_8B_ECC_256_to_250_decoder(uint8_t data_in[256], uint8_t data_out[250], decoder_ctx *context)
{
    uint8_t dec_data_in0[86];
    uint8_t dec_data_in1[86];
    uint8_t dec_data_in2[86];

    uint8_t dec_data_out0[84];
    uint8_t dec_data_out1[84];
    uint8_t dec_data_out2[84];

    for (int i = 0; i < 83; i++)
    {
        dec_data_in0[i] = data_in[i * 3];
        dec_data_in1[i] = data_in[(i * 3) + 1];
        dec_data_in2[i] = data_in[(i * 3) + 2];
    }

    dec_data_in0[83] = data_in[249]; // Blue ECC group 0
    dec_data_in0[84] = data_in[252];
    dec_data_in0[85] = data_in[255];
    dec_data_in1[83] = 0x00; // Orange ECC group 1
    dec_data_in1[84] = data_in[250];
    dec_data_in1[85] = data_in[253];
    dec_data_in2[83] = 0x00; // Green ECC group 2
    dec_data_in2[84] = data_in[251];
    dec_data_in2[85] = data_in[254];

    ECC_86_to_84_decoder(dec_data_in0, dec_data_out0, &(context->ECC_group[0]));
    ECC_86_to_84_decoder(dec_data_in1, dec_data_out1, &(context->ECC_group[1]));
    ECC_86_to_84_decoder(dec_data_in2, dec_data_out2, &(context->ECC_group[2]));

    for (int i = 0; i < 83; i++)
    {
        data_out[i * 3] = dec_data_out0[i];
        data_out[i * 3 + 1] = dec_data_out1[i];
        data_out[i * 3 + 2] = dec_data_out2[i];
    }

    data_out[249] = dec_data_out0[83];
}

void PCI_SIG_8B_ECC_250_to_256_encoder(uint8_t data_in[250], uint8_t data_out[256])
{
    for (int i = 0; i < 250; i++)
    {
        data_out[i] = data_in[i];
    }

    uint8_t ecc_input0[84];
    uint8_t ecc_input1[84];
    uint8_t ecc_input2[84];

    uint8_t ecc_output0[86];
    uint8_t ecc_output1[86];
    uint8_t ecc_output2[86];

    for (int i = 0; i < 83; i++)
    {
        ecc_input0[i] = data_in[i * 3];
        ecc_input1[i] = data_in[i * 3 + 1];
        ecc_input2[i] = data_in[i * 3 + 2];
    }

    ecc_input0[83] = data_in[249];
    ecc_input1[83] = 0x00;
    ecc_input2[83] = 0x00;

    ECC_84_to_86_encoder(ecc_input0, ecc_output0);
    ECC_84_to_86_encoder(ecc_input1, ecc_output1);
    ECC_84_to_86_encoder(ecc_input2, ecc_output2);

    // Next Six bytes are assigned using the check and parity
    // from the three ECC groups.
    data_out[250] = ecc_output1[84]; // ECC1[0] = Check of Group 1.
    data_out[251] = ecc_output2[84]; // ECC2[0] = Check of Group 2.
    data_out[252] = ecc_output0[84]; // ECC0[0] = Check of Group 0.
    data_out[253] = ecc_output1[85]; // ECC1[1] = Parity of Group 1.
    data_out[254] = ecc_output2[85]; // ECC2[1] = Parity of Group 2.
    data_out[255] = ecc_output0[85]; // ECC0[1] = Parity of Group 0.
}