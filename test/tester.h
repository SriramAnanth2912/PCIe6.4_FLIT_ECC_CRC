#ifndef TESTER_H
#define TESTER_H

#include <stdint.h>
#include "config.h"

#define PAYLOAD_LEN 242
#define CRC_OUT_LEN (PAYLOAD_LEN + 8)
#define ECC_OUT_LEN (PAYLOAD_LEN + 8 + 6) /* 256 */

int check_rand_payload(int payload_len);
int check_payload_idx(uint8_t *payload, int idx);
int check_rand_payload_idx(int payload_len, int idx);
int check_payload_idx_with_xor_byte(uint8_t *payload, int idx, int xor_byte);
int check_rand_payload_idx_with_xor_byte(int payload_len, int idx, int xor_byte);
void print_help(void);

#ifdef TCS_ACTIVE
int check_payload_idx_with_xor_byte_withtcs(uint8_t *payload, int idx, int xor_byte);
#endif

#endif /* TESTER_H */