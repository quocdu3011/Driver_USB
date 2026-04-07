#ifndef SECURE_AES_CORE_H
#define SECURE_AES_CORE_H

#include <linux/types.h>

#include "ioctl_defs.h"

struct secure_aes_crypto_params {
    u32 mode;
    u32 key_len;
    u8 key[SECURE_AES_MAX_KEY_SIZE];
    u8 iv[SECURE_AES_IV_SIZE];
};

int aes_cbc_process_buffer(const struct secure_aes_crypto_params *params,
                           const u8 *input, size_t input_len,
                           u8 **output, size_t *output_len);

#endif