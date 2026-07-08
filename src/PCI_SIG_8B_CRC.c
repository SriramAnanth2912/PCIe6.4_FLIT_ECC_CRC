#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "PCI_SIG_8B_CRC.h"

static uint8_t get_crc_byte(uint8_t data_in[242], uint8_t idx)
{
    uint8_t temp_out = 0x00;
    for (int i = 0; i < MAX_ROWS / 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            temp_out ^= (((data_in[i] >> j) & 0x01) ? 0xFF : 0x00) & gen_matrix[MAX_ROWS - 8 * i - j - 1][idx];
        }
    }

    return (uint8_t)temp_out;
}
void PCI_SIG_8B_CRC(uint8_t data_in[242], uint8_t data_out[250])
{
    for (int i = 0; i < 242; i++)
    {
        data_out[i] = data_in[i];
    }

    for (int i = 242; i < 250; i++)
    {
        data_out[i] = get_crc_byte(data_in, (250 - i) - 1);
    }
}