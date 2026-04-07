#ifndef SECURE_AES_HEX_UTILS_H
#define SECURE_AES_HEX_UTILS_H

#include <stddef.h>
#include <stdint.h>

int parse_hex_string(const char *hex,
                     uint8_t *output,
                     size_t *output_len,
                     size_t max_output_len);
int parse_hex_string_exact(const char *hex,
                           uint8_t *output,
                           size_t expected_output_len);

#endif