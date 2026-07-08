#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define MAX_COLS 8

int read_payload(uint8_t *payload, const char *filepath, uint16_t payload_len_bytes)
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
           fscanf(fp, "%2x", &value) == 1)
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