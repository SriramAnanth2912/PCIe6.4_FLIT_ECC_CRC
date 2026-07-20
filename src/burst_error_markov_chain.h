#ifndef BURST_ERROR_MARKOV_CHAIN_H
#define BURST_ERROR_MARKOV_CHAIN_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum
{
    MARKOV_CHAIN_SUCCESS = 0,
    MARKOV_CHAIN_FAILURE = 1
} markov_chain_st;

#define N 256
#define xor_state(A, B, C, D)   \
    for (int i = 0; i < D; i++) \
        (A)[i] = (B)[i] ^ (C)[i];

static inline void bit_to_byte(uint8_t *in, uint8_t *out, int inlen)
{
    for (int i = 0; i < inlen; i += 8)
    {
        uint8_t temp = 0;
        for (int j = 0; j < 8; j++)
        {
            temp = (temp << 1) | in[i + j];
        }
        out[i / 8] = temp;
    }
}

markov_chain_st burst_error_markov_chain(uint8_t input[N], uint8_t *output, uint8_t *channel, float p_good, float p_bad, float p_gb, float p_bg)
{
    // reference: https://www.edaboard.com/threads/how-to-generate-burst-errors.227695/
    // Two-state (Gilbert-Elliott) burst error channel model
    //
    // Inputs:
    //   channel_in  - input byte array
    //   p_good      - bit error probability in Good (random) state  [low,  e.g. 0.01]
    //   p_bad       - bit error probability in Bad  (burst) state   [high, e.g. 0.30]
    //   p_gb        - transition probability Good -> Bad             [low,  e.g. 0.05]
    //   p_bg        - transition probability Bad  -> Good            [moderate, e.g. 0.20]

    uint8_t *output_temp = (uint8_t *)calloc(N, sizeof(uint8_t));
    uint8_t *channel_temp = (uint8_t *)calloc(N, sizeof(uint8_t));
    uint8_t *xor_bits = (uint8_t *)calloc(N * 8, sizeof(uint8_t));
    int N_bits = N * 8;
    int state = 1;

    for (int i = 0; i < N_bits; i++)
    {
        if (state == 1)
            xor_bits[i] = (int)((float)((double)rand() / RAND_MAX) < p_good);
        else
            xor_bits[i] = (int)((float)((double)rand() / RAND_MAX) < p_bad);

        if (state == 1)
            state = 1 + (int)((float)((double)rand() / RAND_MAX) < p_gb);
        else
            state = 2 - (int)((float)((double)rand() / RAND_MAX) < p_bg);
    }

    bit_to_byte(xor_bits, channel_temp, N * 8);
    xor_state(output_temp, channel_temp, input, N);

    memcpy(output, output_temp, N);
    memcpy(channel, channel_temp, N);

    free(output_temp);
    free(channel_temp);
    free(xor_bits);

    return MARKOV_CHAIN_SUCCESS;
}
#endif /* BURST_ERROR_MARKOV_CHAIN_H */
